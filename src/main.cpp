// Pathfinder: brute-force path search using the game's own physics, at 60 FPS.
//
//  * Pressing "Start pathfind" restarts the level (practice mode) and simulates it frame by
//    frame (1/60 s per frame = 4 physics ticks), as fast as possible.
//  * Every frame is a decision point: the bot saves a checkpoint, first keeps the current input,
//    and if the player dies it loads the checkpoint and tries the opposite input. If both fail it
//    goes further back (depth-first search with backtracking).
//  * Real game physics is used, so all gamemodes, orbs, pads, portals and triggers just work.
//  * Player states proven deadly are remembered (hashed) so they are not retried.
//  * Going back further than the "dead end threshold" after a death marks that spot as a dead end.
//  * The found path is exported as a 60 FPS GDR (.gdr.json) macro for Mega Hack v9.
//  * The current path is drawn green, deaths are drawn as red dots.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <unordered_set>

using namespace geode::prelude;

static constexpr float FPS = 60.f;

struct InputEvent {
    int frame;
    bool down;
};

struct Node {
    int frame;
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
    int frame = 0;

    bool replaying = false;
    int replayTarget = 0;
    size_t replayPos = 0;

    int step = 1;
    int budgetMs = 14;
    size_t maxCheckpoints = 3000;
    int deadEndFrames = 30;
    bool drawTrajectory = true;

    std::vector<Node> stack;
    size_t firstLive = 0;
    std::vector<InputEvent> inputs;
    std::unordered_set<uint64_t> dead;
    std::vector<float> deadEnds;
    float bestPercent = 0.f;
    int backtracks = 0;

    std::vector<CCPoint> path;    // player position per frame (object layer space)
    std::vector<CCPoint> deaths;  // recent death positions
    CCDrawNode* drawNode = nullptr;
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

static uint64_t stateHash(PlayLayer* pl) {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, uint64_t(g_pf.frame));
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

static CCPoint playerPos(PlayLayer* pl) {
    auto p = pl->m_player1;
    if (!p || !p->getParent() || !pl->m_objectLayer) return {0.f, 0.f};
    auto world = p->getParent()->convertToWorldSpace(p->getPosition());
    return pl->m_objectLayer->convertToNodeSpace(world);
}

static void recordPosition(PlayLayer* pl) {
    if (static_cast<int>(g_pf.path.size()) > g_pf.frame) g_pf.path.resize(g_pf.frame);
    g_pf.path.push_back(playerPos(pl));
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

// ---------------- trajectory ----------------

static void ensureDrawNode(PlayLayer* pl) {
    if (g_pf.drawNode || !pl->m_objectLayer) return;
    g_pf.drawNode = CCDrawNode::create();
    pl->m_objectLayer->addChild(g_pf.drawNode, 9999);
}

static void drawTrajectory() {
    if (!g_pf.drawNode) return;
    g_pf.drawNode->clear();
    if (!g_pf.drawTrajectory) return;

    ccColor4F green = {0.f, 1.f, 0.3f, 1.f};
    ccColor4F red = {1.f, 0.1f, 0.1f, 0.8f};

    for (size_t i = 1; i < g_pf.path.size(); i++) {
        auto& a = g_pf.path[i - 1];
        auto& b = g_pf.path[i];
        if (std::abs(a.x - b.x) > 300.f || std::abs(a.y - b.y) > 300.f) continue; // teleports
        g_pf.drawNode->drawSegment(a, b, 1.2f, green);
    }
    for (auto& d : g_pf.deaths) {
        g_pf.drawNode->drawDot(d, 2.5f, red);
    }
}

// ---------------- macro export (GDR, 60 FPS) ----------------

static std::string saveMacro(PlayLayer* pl) {
    std::string name = pl->m_level ? std::string(pl->m_level->m_levelName) : "level";
    int levelID = pl->m_level ? pl->m_level->m_levelID.value() : 0;
    float duration = g_pf.frame / FPS;

    std::string j = "{";
    j += fmt::format("\"gameVersion\":2.2081,\"description\":\"Generated by Pathfinder\",\"version\":1.0,\"duration\":{:.3f},", duration);
    j += "\"bot\":{\"name\":\"Pathfinder\",\"version\":\"1.1.0\"},";
    j += fmt::format("\"level\":{{\"id\":{},\"name\":\"{}\"}},", levelID, jsonEscape(name));
    j += "\"author\":\"Pathfinder\",\"seed\":0,\"coins\":0,\"ldm\":false,\"framerate\":60.0,\"inputs\":[";
    for (size_t i = 0; i < g_pf.inputs.size(); i++) {
        auto& e = g_pf.inputs[i];
        j += fmt::format("{}{{\"frame\":{},\"btn\":1,\"2p\":false,\"down\":{}}}",
            i ? "," : "", e.frame, e.down ? "true" : "false");
    }
    j += "]}";

    auto fileName = safeFileName(name) + "_pathfinder.gdr.json";
    auto path = Mod::get()->getSaveDir() / fileName;
    {
        std::ofstream f(path, std::ios::binary);
        f << j;
    }

    auto mhDir = dirs::getModsSaveDir() / "absolllute.megahack" / "replays";
    std::error_code ec;
    if (std::filesystem::exists(mhDir, ec)) {
        std::ofstream f2(mhDir / fileName, std::ios::binary);
        f2 << j;
    }
    return pathToUtf8(path);
}

// ---------------- search ----------------

static void resetToStart(PlayLayer* pl) {
    pl->resetLevel();
    rawButton(pl, false);
    g_pf.held = false;
    g_pf.frame = 0;
}

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
            "Path found! (60 FPS macro)\nInputs: {}, backtracks: {}\nDead ends: {}\n\nMacro saved to:\n{}",
            g_pf.inputs.size(), g_pf.backtracks, formatDeadEnds(), path);
    } else {
        text = fmt::format(
            "{}\nBest progress: {:.1f}%\nBacktracks: {}\nDead ends: {}",
            reason.empty() ? "No path found - the level looks impossible with these settings." : reason,
            g_pf.bestPercent, g_pf.backtracks, formatDeadEnds());
    }
    log::info("Pathfinder finished: {}", text);

    rawButton(pl, false);
    if (pl->m_isPracticeMode) pl->togglePracticeMode(false);
    pl->resetLevel();
    drawTrajectory(); // keep the final path visible

    Loader::get()->queueInMainThread([text] {
        FLAlertLayer::create("Pathfinder", text, "OK")->show();
    });
}

static void applyAlternative(PlayLayer* pl, Node& n) {
    rawButton(pl, false);
    bool want = !n.prevHeld;
    if (want) rawButton(pl, true);
    g_pf.held = want;
    g_pf.inputs.push_back({n.frame, want});
}

static void backtrack(PlayLayer* pl) {
    g_pf.backtracks++;
    int deathFrame = g_pf.frame;
    float deathPercent = pl->getCurrentPercent();

    g_pf.deaths.push_back(playerPos(pl));
    if (g_pf.deaths.size() > 400) g_pf.deaths.erase(g_pf.deaths.begin());

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
    if (deathFrame - n.frame > g_pf.deadEndFrames) addDeadEnd(deathPercent);

    g_pf.inputs.resize(n.inputsSize);
    if (static_cast<int>(g_pf.path.size()) > n.frame + 1) g_pf.path.resize(n.frame + 1);

    if (n.cp) {
        pl->loadFromCheckpoint(n.cp);
        g_pf.frame = n.frame;
        applyAlternative(pl, n);
    } else {
        // checkpoint was dropped to save memory: replay from the start up to this node
        g_pf.replaying = true;
        g_pf.replayTarget = n.frame;
        g_pf.replayPos = 0;
        resetToStart(pl);
    }
}

static void pushNode(PlayLayer* pl) {
    uint64_t h = stateHash(pl);
    if (g_pf.dead.count(h)) {
        backtrack(pl);
        return;
    }

    Node n{g_pf.frame, pl->createCheckpoint(), h, g_pf.held, 0, g_pf.inputs.size()};
    if (n.cp) n.cp->retain();
    else log::warn("Pathfinder: createCheckpoint returned null at frame {}", g_pf.frame);
    g_pf.stack.push_back(n);

    while (g_pf.stack.size() - g_pf.firstLive > g_pf.maxCheckpoints) {
        auto& old = g_pf.stack[g_pf.firstLive];
        if (old.cp) old.cp->release();
        old.cp = nullptr;
        g_pf.firstLive++;
    }
}

static void startSearch(PlayLayer* pl) {
    log::info("Pathfinder: starting");
    releaseAllCheckpoints();
    g_pf.inputs.clear();
    g_pf.dead.clear();
    g_pf.deadEnds.clear();
    g_pf.path.clear();
    g_pf.deaths.clear();
    g_pf.bestPercent = 0.f;
    g_pf.backtracks = 0;
    g_pf.died = false;
    g_pf.completed = false;
    g_pf.stopRequested = false;
    g_pf.replaying = false;

    g_pf.step = static_cast<int>(Mod::get()->getSettingValue<int64_t>("step"));
    g_pf.budgetMs = static_cast<int>(Mod::get()->getSettingValue<int64_t>("budget"));
    g_pf.maxCheckpoints = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-checkpoints"));
    g_pf.deadEndFrames = static_cast<int>(Mod::get()->getSettingValue<int64_t>("dead-end-frames"));
    g_pf.drawTrajectory = Mod::get()->getSettingValue<bool>("trajectory");

    // restart the level from scratch
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    resetToStart(pl);

    ensureDrawNode(pl);

    if (!g_pf.label) {
        auto win = CCDirector::get()->getWinSize();
        g_pf.label = CCLabelBMFont::create("Pathfinding...", "bigFont.fnt");
        g_pf.label->setAnchorPoint({0.f, 1.f});
        g_pf.label->setScale(0.35f);
        g_pf.label->setPosition({5.f, win.height - 5.f});
        pl->addChild(g_pf.label, 10000);
    }

    recordPosition(pl);
    pushNode(pl);
}

static void updateLabel(PlayLayer* pl) {
    if (!g_pf.label) return;
    float pct = pl->getCurrentPercent();
    if (pct > g_pf.bestPercent) g_pf.bestPercent = pct;
    g_pf.label->setString(fmt::format(
        "Pathfinding{}: {:.1f}% (best {:.1f}%)\nFrame {}  Backtracks: {}  Dead ends: {}",
        g_pf.replaying ? " (replaying)" : "", pct, g_pf.bestPercent, g_pf.frame,
        g_pf.backtracks, g_pf.deadEnds.size()).c_str());
}

// ---------------- hooks ----------------

class $modify(PFGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_pf.running && !g_pf.botInput) return; // block real input during the search
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
                       g_pf.inputs[g_pf.replayPos].frame <= g_pf.frame) {
                    bool d = g_pf.inputs[g_pf.replayPos].down;
                    rawButton(pl, d);
                    g_pf.held = d;
                    g_pf.replayPos++;
                }
            }

            // one 60 FPS frame = 4 physics ticks
            g_pf.died = false;
            GJBaseGameLayer::update(1.f / FPS);
            g_pf.frame++;
            if (!g_pf.running) break;

            if (g_pf.completed || pl->getCurrentPercent() >= 100.f) {
                recordPosition(pl);
                finish(pl, true);
                break;
            }

            if (g_pf.replaying) {
                if (g_pf.died) {
                    finish(pl, false, "Replay desync (the level is not deterministic).");
                    break;
                }
                if (g_pf.frame >= g_pf.replayTarget) {
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

            recordPosition(pl);

            if (!g_pf.stack.empty() && g_pf.frame >= g_pf.stack.back().frame + g_pf.step) {
                pushNode(pl);
            }
        }

        if (g_pf.running) {
            updateLabel(pl);
            drawTrajectory();
        }
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
        g_pf.running = false;
        g_pf.needsStart = false;
        g_pf.replaying = false;
        releaseAllCheckpoints();
        g_pf.label = nullptr;    // destroyed together with the layer
        g_pf.drawNode = nullptr; // same
        g_pf.path.clear();
        g_pf.deaths.clear();
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
            "Restarts the level and finds a path by trying inputs with the real game physics "
            "(all gamemodes, orbs and pads work). Draws the trajectory, reports dead ends and saves a "
            "<cy>60 FPS Mega Hack v9</c> macro (.gdr.json).\n\n"
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
