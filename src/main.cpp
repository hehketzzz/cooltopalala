// Pathfinder: brute-force path search using the game's own physics.
//
// How it works:
//  * The level is restarted in practice mode and simulated at 240 TPS as fast as possible.
//  * Every N ticks is a "decision point": the bot saves a checkpoint and first tries to keep
//    the current input (held / released). If the player dies, it loads the checkpoint and tries
//    the opposite input. If both fail, it goes further back (depth-first search with backtracking).
//  * Because the real game physics is used, every gamemode, orb, pad, portal and trigger
//    works automatically - the bot doesn't need to "understand" them.
//  * States that were proven deadly are remembered (hash of player state), so the search
//    doesn't retry identical situations.
//  * If after dying the bot has to go back far (more than "dead end threshold"), that place is
//    reported as a dead end: you could reach it alive, but there was no way to survive from there.
//  * When the level is completed, inputs are saved as a GDR (.gdr.json) macro for Mega Hack v9.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <unordered_set>

using namespace geode::prelude;

struct InputEvent {
    int frame;
    bool down;
};

struct Node {
    int tick;
    CheckpointObject* cp;
    uint64_t hash;
    bool prevHeld;
    int tried;
    size_t inputsSize;
};

struct PFState {
    bool running = false;
    bool needsStart = false;
    bool stopRequested = false;
    bool died = false;
    bool completed = false;
    bool botInput = false;
    bool held = false;

    bool replaying = false;
    int replayTarget = 0;
    size_t replayPos = 0;

    int step = 4;
    int budgetMs = 14;
    size_t maxCheckpoints = 3000;
    int deadEndTicks = 120;

    std::vector<Node> stack;
    size_t firstLive = 0;
    std::vector<InputEvent> inputs;
    std::unordered_set<uint64_t> dead;
    std::vector<float> deadEnds;
    float bestPercent = 0.f;
    int backtracks = 0;
    CCLabelBMFont* label = nullptr;
};

static PFState g_pf;

// ---------------- helpers ----------------

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

template <class T>
static uint64_t bitsOf(T v) {
    uint64_t r = 0;
    std::memcpy(&r, &v, sizeof(T) < sizeof(r) ? sizeof(T) : sizeof(r));
    return r;
}

static uint64_t hashPlayer(uint64_t h, PlayerObject* p) {
    if (!p) return h;
    h = mix(h, bitsOf(p->getPositionX()));
    h = mix(h, bitsOf(p->getPositionY()));
    h = mix(h, bitsOf(p->m_yVelocity));
    h = mix(h, bitsOf(p->m_vehicleSize));
    h = mix(h, bitsOf(p->m_playerSpeed));
    uint64_t flags =
        (uint64_t(p->m_isShip) << 0) | (uint64_t(p->m_isBird) << 1) |
        (uint64_t(p->m_isBall) << 2) | (uint64_t(p->m_isDart) << 3) |
        (uint64_t(p->m_isRobot) << 4) | (uint64_t(p->m_isSpider) << 5) |
        (uint64_t(p->m_isSwing) << 6) | (uint64_t(p->m_isUpsideDown) << 7);
    return mix(h, flags);
}

static int currentTick(PlayLayer* pl) {
    return pl->m_gameState.m_currentProgress;
}

static uint64_t stateHash(PlayLayer* pl) {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, uint64_t(currentTick(pl)));
    h = mix(h, uint64_t(g_pf.held));
    h = hashPlayer(h, pl->m_player1);
    h = hashPlayer(h, pl->m_player2);
    return h;
}

static void rawButton(PlayLayer* pl, bool down) {
    g_pf.botInput = true;
    pl->handleButton(down, 1, true);
    g_pf.botInput = false;
}

static std::string jsonEscape(std::string const& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    return out;
}

static std::string safeFileName(std::string s) {
    for (auto& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') c = '_';
    }
    if (s.empty()) s = "level";
    return s;
}

static std::string pathToUtf8(std::filesystem::path const& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<char const*>(u.c_str()), u.size());
}

static std::string formatDeadEnds() {
    if (g_pf.deadEnds.empty()) return "none";
    std::string s;
    for (size_t i = 0; i < g_pf.deadEnds.size(); i++) {
        if (i) s += ", ";
        s += fmt::format("{:.1f}%", g_pf.deadEnds[i]);
    }
    return s;
}

static void addDeadEnd(float percent) {
    for (float p : g_pf.deadEnds) {
        if (std::abs(p - percent) < 1.5f) return;
    }
    g_pf.deadEnds.push_back(percent);
    std::sort(g_pf.deadEnds.begin(), g_pf.deadEnds.end());
}

static void releaseAllCheckpoints() {
    for (auto& n : g_pf.stack) {
        if (n.cp) n.cp->release();
        n.cp = nullptr;
    }
    g_pf.stack.clear();
    g_pf.firstLive = 0;
}

// ---------------- macro export ----------------

static std::string saveMacro(PlayLayer* pl) {
    std::string name = pl->m_level ? std::string(pl->m_level->m_levelName) : "level";
    int levelID = pl->m_level ? pl->m_level->m_levelID.value() : 0;
    float duration = g_pf.inputs.empty() ? 0.f : g_pf.inputs.back().frame / 240.f;

    std::string j = "{";
    j += fmt::format("\"gameVersion\":2.2081,\"description\":\"Generated by Pathfinder\",\"version\":1.0,\"duration\":{:.3f},", duration);
    j += "\"bot\":{\"name\":\"Pathfinder\",\"version\":\"1.0.0\"},";
    j += fmt::format("\"level\":{{\"id\":{},\"name\":\"{}\"}},", levelID, jsonEscape(name));
    j += "\"author\":\"Pathfinder\",\"seed\":0,\"coins\":0,\"ldm\":false,\"framerate\":240.0,\"inputs\":[";
    for (size_t i = 0; i < g_pf.inputs.size(); i++) {
        auto& e = g_pf.inputs[i];
        j += fmt::format("{}{{\"frame\":{},\"btn\":1,\"2p\":false,\"down\":{}}}",
            i ? "," : "", e.frame, e.down ? "true" : "false");
    }
    j += "]}";

    auto fileName = safeFileName(name) + "_pathfinder.gdr.json";
    auto path = Mod::get()->getSaveDir() / fileName;
    std::ofstream f(path, std::ios::binary);
    f << j;
    f.close();

    // Also try to drop it straight into Mega Hack's replay folder if it exists
    auto mhDir = dirs::getModsSaveDir() / "absolllute.megahack" / "replays";
    std::error_code ec;
    if (std::filesystem::exists(mhDir, ec)) {
        std::ofstream f2(mhDir / fileName, std::ios::binary);
        f2 << j;
    }
    return pathToUtf8(path);
}

// ---------------- search ----------------

static void finish(PlayLayer* pl, bool success, std::string const& reason = "") {
    g_pf.running = false;
    g_pf.replaying = false;
    releaseAllCheckpoints();
    if (g_pf.label) {
        g_pf.label->removeFromParent();
        g_pf.label = nullptr;
    }

    std::string text;
    if (success) {
        auto path = saveMacro(pl);
        text = fmt::format(
            "Path found!\nInputs: {}, backtracks: {}\nDead ends: {}\n\nMacro saved to:\n{}",
            g_pf.inputs.size(), g_pf.backtracks, formatDeadEnds(), path);
    } else {
        text = fmt::format(
            "{}\nBest progress: {:.1f}%\nBacktracks: {}\nDead ends: {}",
            reason.empty() ? "No path found - the level looks impossible with these settings." : reason,
            g_pf.bestPercent, g_pf.backtracks, formatDeadEnds());
    }

    rawButton(pl, false);
    if (pl->m_isPracticeMode) pl->togglePracticeMode(false);
    pl->resetLevel();

    Loader::get()->queueInMainThread([text] {
        FLAlertLayer::create("Pathfinder", text, "OK")->show();
    });
}

static void applyAlternative(PlayLayer* pl, Node& n) {
    // normalize button state, then apply the opposite of what was tried first
    rawButton(pl, false);
    bool want = !n.prevHeld;
    if (want) rawButton(pl, true);
    g_pf.held = want;
    g_pf.inputs.push_back({n.tick, want});
}

static void backtrack(PlayLayer* pl) {
    g_pf.backtracks++;
    int deathTick = currentTick(pl);
    float deathPercent = pl->getCurrentPercent();

    while (!g_pf.stack.empty() && g_pf.stack.back().tried >= 1) {
        auto& b = g_pf.stack.back();
        g_pf.dead.insert(b.hash);
        if (b.cp) b.cp->release();
        g_pf.stack.pop_back();
    }
    if (g_pf.firstLive > g_pf.stack.size()) g_pf.firstLive = g_pf.stack.size();

    if (g_pf.stack.empty()) {
        finish(pl, false);
        return;
    }

    auto& n = g_pf.stack.back();
    n.tried = 1;
    if (deathTick - n.tick > g_pf.deadEndTicks) addDeadEnd(deathPercent);

    g_pf.inputs.resize(n.inputsSize);

    if (n.cp) {
        pl->loadFromCheckpoint(n.cp);
        applyAlternative(pl, n);
    } else {
        // checkpoint was dropped to save memory: replay from the start up to this node
        g_pf.replaying = true;
        g_pf.replayTarget = n.tick;
        g_pf.replayPos = 0;
        pl->resetLevel();
        rawButton(pl, false);
        g_pf.held = false;
    }
}

static void pushNode(PlayLayer* pl) {
    uint64_t h = stateHash(pl);
    if (g_pf.dead.count(h)) {
        backtrack(pl);
        return;
    }

    Node n{currentTick(pl), pl->createCheckpoint(), h, g_pf.held, 0, g_pf.inputs.size()};
    if (n.cp) n.cp->retain();
    g_pf.stack.push_back(n);

    while (g_pf.stack.size() - g_pf.firstLive > g_pf.maxCheckpoints) {
        auto& old = g_pf.stack[g_pf.firstLive];
        if (old.cp) old.cp->release();
        old.cp = nullptr;
        g_pf.firstLive++;
    }
}

static void startSearch(PlayLayer* pl) {
    releaseAllCheckpoints();
    g_pf.inputs.clear();
    g_pf.dead.clear();
    g_pf.deadEnds.clear();
    g_pf.bestPercent = 0.f;
    g_pf.backtracks = 0;
    g_pf.died = false;
    g_pf.completed = false;
    g_pf.stopRequested = false;
    g_pf.replaying = false;

    g_pf.step = static_cast<int>(Mod::get()->getSettingValue<int64_t>("step"));
    g_pf.budgetMs = static_cast<int>(Mod::get()->getSettingValue<int64_t>("budget"));
    g_pf.maxCheckpoints = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-checkpoints"));
    g_pf.deadEndTicks = static_cast<int>(Mod::get()->getSettingValue<int64_t>("dead-end-ticks"));

    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    pl->resetLevel();
    rawButton(pl, false);
    g_pf.held = false;

    if (!g_pf.label) {
        auto win = CCDirector::get()->getWinSize();
        g_pf.label = CCLabelBMFont::create("Pathfinding...", "bigFont.fnt");
        g_pf.label->setAnchorPoint({0.f, 1.f});
        g_pf.label->setScale(0.35f);
        g_pf.label->setPosition({5.f, win.height - 5.f});
        pl->addChild(g_pf.label, 10000);
    }

    pushNode(pl);
}

static void updateLabel(PlayLayer* pl) {
    if (!g_pf.label) return;
    float pct = pl->getCurrentPercent();
    if (pct > g_pf.bestPercent) g_pf.bestPercent = pct;
    g_pf.label->setString(fmt::format(
        "Pathfinding{}: {:.1f}% (best {:.1f}%)\nBacktracks: {}  Dead ends: {}",
        g_pf.replaying ? " (replaying)" : "", pct, g_pf.bestPercent,
        g_pf.backtracks, g_pf.deadEnds.size()).c_str());
}

// ---------------- hooks ----------------

class $modify(PFGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        // block the real player's input while the bot is searching
        if (g_pf.running && !g_pf.botInput) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!g_pf.running || !pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }

        if (g_pf.needsStart) {
            g_pf.needsStart = false;
            startSearch(pl);
        }
        if (g_pf.stopRequested) {
            finish(pl, false, "Stopped by user.");
            return;
        }

        auto t0 = std::chrono::steady_clock::now();
        while (g_pf.running) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= g_pf.budgetMs) break;

            if (g_pf.replaying) {
                while (g_pf.replayPos < g_pf.inputs.size() &&
                       g_pf.inputs[g_pf.replayPos].frame <= currentTick(pl)) {
                    bool d = g_pf.inputs[g_pf.replayPos].down;
                    rawButton(pl, d);
                    g_pf.held = d;
                    g_pf.replayPos++;
                }
            }

            g_pf.died = false;
            GJBaseGameLayer::update(1.f / 240.f);
            if (!g_pf.running) break;

            if (g_pf.completed) {
                finish(pl, true);
                break;
            }

            if (g_pf.replaying) {
                if (g_pf.died) {
                    finish(pl, false, "Replay desync (the level is not deterministic).");
                    break;
                }
                if (currentTick(pl) >= g_pf.replayTarget) {
                    g_pf.replaying = false;
                    auto& n = g_pf.stack.back();
                    n.cp = pl->createCheckpoint();
                    if (n.cp) n.cp->retain();
                    g_pf.firstLive = g_pf.stack.size() - 1;
                    applyAlternative(pl, n);
                }
                continue;
            }

            if (g_pf.died) {
                backtrack(pl);
                continue;
            }

            if (!g_pf.stack.empty() && currentTick(pl) >= g_pf.stack.back().tick + g_pf.step) {
                pushNode(pl);
            }
        }

        if (g_pf.running) updateLabel(pl);
    }
};

class $modify(PFPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_pf.running && obj != m_anticheatSpike) {
            g_pf.died = true;
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        if (g_pf.running) {
            g_pf.completed = true;
            return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (g_pf.running || g_pf.needsStart) {
            g_pf.running = false;
            g_pf.needsStart = false;
            g_pf.replaying = false;
            releaseAllCheckpoints();
            g_pf.label = nullptr; // destroyed together with the layer
        }
        PlayLayer::onQuit();
    }
};

class $modify(PFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto spr = ButtonSprite::create("PF");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PFPauseLayer::onPathfinder));
        btn->setID("pathfinder-button"_spr);

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto win = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->addChild(btn);
            menu2->setPosition({win.width - 35.f, win.height / 2.f});
            this->addChild(menu2);
        }
    }

    void onPathfinder(CCObject*) {
        if (g_pf.running) {
            createQuickPopup(
                "Pathfinder",
                fmt::format("Pathfinding is running.\nBest: {:.1f}%  Backtracks: {}\nDead ends: {}",
                    g_pf.bestPercent, g_pf.backtracks, formatDeadEnds()),
                "Close", "Stop",
                [](FLAlertLayer*, bool btn2) {
                    if (btn2) g_pf.stopRequested = true;
                });
            return;
        }

        createQuickPopup(
            "Pathfinder",
            "Finds a path through the level by trying inputs with the real game physics "
            "(all gamemodes, orbs and pads work), reports dead ends and saves a "
            "<cy>Mega Hack v9</c> macro (.gdr.json).\n\n"
            "The level restarts in <cg>practice mode</c> while searching. "
            "Open this menu again to stop.",
            "Cancel", "Start pathfind",
            [this](FLAlertLayer*, bool btn2) {
                if (!btn2) return;
                g_pf.needsStart = true;
                g_pf.running = true;
                this->onResume(nullptr);
            });
    }
};
