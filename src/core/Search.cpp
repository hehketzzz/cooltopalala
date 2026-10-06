#include "Search.hpp"
#include "Gamemode.hpp"
#include "Settings.hpp"
#include "StateHash.hpp"
#include "macro/Macro.hpp"
#include "sim/Trajectory.hpp"
#include "ui/Overlay.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

namespace pf::search {

namespace {

enum class Phase { Idle, Searching, Replaying, Verifying };

struct Node {
    uint32_t tick;          // m_currentProgress when the decision was made
    CheckpointObject* cp;   // may be dropped (nullptr) to save memory
    uint64_t hash;
    bool heldBefore;        // button state before the decision
    bool first;             // input tried first
    uint8_t tried;          // 0 = first choice running, 1 = second choice running
    size_t inputsSize;      // inputs recorded before the decision
    size_t pathSize;
};

struct State {
    Phase phase = Phase::Idle;
    bool startRequested = false;
    bool stopRequested = false;
    bool died = false;
    bool levelEnded = false;
    bool botInput = false;
    bool held = false;
    bool platformer = false;
    bool enabledPractice = false;
    bool fromStartPos = false;

    Settings cfg;

    std::vector<Node> stack;
    size_t firstLive = 0;
    std::vector<macro::Input> inputs;
    std::unordered_set<uint64_t> dead;

    // replay (dropped checkpoint) / verification
    size_t replayPos = 0;
    uint32_t replayTarget = 0;
    uint32_t verifyDeadline = 0;
    uint32_t pathEndTick = 0;

    std::vector<float> deadEnds;
    float bestPercent = 0.f;
    uint64_t backtracks = 0;
    uint64_t steps = 0;
    Gamemode mode = Gamemode::Cube;
    std::chrono::steady_clock::time_point startedAt;

    std::vector<CCPoint> path;
    std::vector<CCPoint> deaths;
};

State s;
constexpr size_t MAX_DEAD_STATES = 4'000'000;
constexpr size_t MAX_DEATH_MARKS = 400;

// ---------------- helpers ----------------

uint32_t curTick(PlayLayer* pl) { return pl->m_gameState.m_currentProgress; }

void sendButton(PlayLayer* pl, int button, bool down) {
    s.botInput = true;
    pl->handleButton(down, button, true);
    s.botInput = false;
}

// change the jump button, recording the input for the macro
void setHeld(PlayLayer* pl, bool want) {
    if (want == s.held) return;
    sendButton(pl, 1, want);
    s.inputs.push_back({curTick(pl), 1, false, want});
    s.held = want;
}

void forEachPlayer(PlayLayer* pl, auto&& fn) {
    if (pl->m_player1) fn(pl->m_player1);
    if (pl->m_player2) fn(pl->m_player2);
}

// After resetLevel / loadFromCheckpoint the game may still have stale button state from the
// branch that died. Put it back to exactly what a bot playing the macro would have.
void restoreButtonState(PlayLayer* pl, bool held) {
    pl->m_queuedButtons.clear();
    forEachPlayer(pl, [&](PlayerObject* p) { p->m_holdingButtons[1] = held; });
    s.held = held;
    if (s.platformer && pl->m_player1 && !pl->m_player1->m_holdingRight) {
        // holding right is not edge sensitive, so this does not need to be recorded
        sendButton(pl, 3, true);
    }
}

void resetToStart(PlayLayer* pl) {
    pl->resetLevel();
    pl->m_queuedButtons.clear();
    forEachPlayer(pl, [](PlayerObject* p) {
        p->m_holdingButtons[1] = false;
        p->m_holdingButtons[3] = false;
    });
    s.held = false;
}

bool reachedEnd(PlayLayer* pl) {
    if (s.levelEnded || pl->m_levelEndAnimationStarted) return true;
    return !s.platformer && pl->getCurrentPercent() >= 100.f;
}

void stepOnce(PlayLayer* pl, StepFn const& step) {
    s.died = false;
    // exactly `ticksPerStep` physics ticks per call; the real tick count is read back from
    // m_currentProgress anyway, so rounding can never desync the macro.
    pl->m_extraDelta = 0.0;
    step(static_cast<float>((s.cfg.ticksPerStep + 0.5) / TPS));
    s.steps++;
}

void recordPosition(PlayLayer* pl) { s.path.push_back(overlay::playerPos(pl)); }

void releaseCheckpoints() {
    for (auto& n : s.stack) {
        if (n.cp) n.cp->release();
        n.cp = nullptr;
    }
    s.stack.clear();
    s.firstLive = 0;
}

std::string formatDeadEnds() {
    if (s.deadEnds.empty()) return "none";
    std::string out;
    for (size_t i = 0; i < s.deadEnds.size(); i++) {
        if (i) out += ", ";
        out += fmt::format("{:.1f}%", s.deadEnds[i]);
    }
    return out;
}

void addDeadEnd(float percent) {
    for (float p : s.deadEnds)
        if (std::abs(p - percent) < 1.5f) return;
    s.deadEnds.push_back(percent);
    std::sort(s.deadEnds.begin(), s.deadEnds.end());
}

double elapsedSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - s.startedAt).count();
}

// ---------------- input choice ----------------

int survival(PlayLayer* pl, bool hold) {
    int ticks = s.cfg.lookaheadTicks;
    auto a = traj::predict(pl, pl->m_player1, hold, ticks, false);
    int result = a.died ? a.survivedTicks : ticks + 1;
    if (pl->m_gameState.m_isDualMode && pl->m_player2) {
        auto b = traj::predict(pl, pl->m_player2, hold, ticks, false);
        result = std::min(result, b.died ? b.survivedTicks : ticks + 1);
    }
    return result;
}

bool chooseFirstInput(PlayLayer* pl) {
    s.mode = detectGamemode(pl->m_player1);
    // gamemode default: continuous modes keep the current input, click modes stay released
    bool def = isContinuousMode(s.mode) ? s.held : false;
    if (!s.cfg.smartOrder) return def;

    int keep = survival(pl, def);
    if (keep > s.cfg.lookaheadTicks) return def; // default survives the whole lookahead
    int other = survival(pl, !def);
    return other > keep ? !def : def;
}

// ---------------- finishing ----------------

void finish(PlayLayer* pl, bool success, std::string const& reason, int verified /* -1 off, 0 fail, 1 ok */,
            float verifyPercent = 0.f) {
    s.phase = Phase::Idle;
    releaseCheckpoints();
    s.dead.clear();
    overlay::removeLabel();
    traj::clearDrawing();

    std::string text;
    if (success) {
        uint32_t lastTick = s.pathEndTick ? s.pathEndTick : curTick(pl);
        macro::Info info;
        info.levelName = pl->m_level ? std::string(pl->m_level->m_levelName) : "level";
        info.levelID = pl->m_level ? pl->m_level->m_levelID.value() : 0;
        info.platformer = s.platformer;
        info.framerate = TPS;
        info.lastTick = lastTick;
        auto res = macro::exportMacro(info, s.inputs, s.cfg.exportDir, s.cfg.exportFormat);

        std::string files;
        for (auto& f : res.files) files += "\n" + macro::pathToUtf8(f);
        for (auto& e : res.errors) files += "\n<cr>" + e + "</c>";

        std::string verifyLine;
        if (verified == 1) verifyLine = "<cg>Verified: replay from the start completes the level.</c>\n";
        else if (verified == 0)
            verifyLine = fmt::format("<cr>Verification failed at {:.1f}% - the level may be non-deterministic "
                                     "(try disabling Smart input order).</c>\n", verifyPercent);

        text = fmt::format(
            "Path found in {:.0f}s! (240 TPS GDR macro)\n{}Inputs: {}, backtracks: {}\nDead ends: {}{}\n\nMacro saved to:{}",
            elapsedSec(), verifyLine, s.inputs.size(), s.backtracks, formatDeadEnds(),
            s.fromStartPos ? "\n<cy>Note: the run started from a start position.</c>" : "", files);
    } else {
        text = fmt::format("{}\nBest progress: {:.1f}%\nBacktracks: {}\nDead ends: {}",
            reason.empty() ? "No path found - the level looks impossible with these settings." : reason,
            s.bestPercent, s.backtracks, formatDeadEnds());
    }
    log::info("Pathfinder finished: {}", text);

    sendButton(pl, 1, false);
    if (s.platformer) sendButton(pl, 3, false);
    s.held = false;
    if (s.enabledPractice && pl->m_isPracticeMode) pl->togglePracticeMode(false);
    pl->resetLevel();
    overlay::drawPath(s.path, s.deaths, s.cfg.drawPath); // keep the final path visible

    Loader::get()->queueInMainThread([text] {
        FLAlertLayer::create(nullptr, "Pathfinder", text, "OK", nullptr, 380.f)->show();
    });
}

void startVerification(PlayLayer* pl) {
    s.phase = Phase::Verifying;
    s.levelEnded = false;
    s.replayPos = 0;
    s.verifyDeadline = curTick(pl) + static_cast<uint32_t>(TPS * 3);
    releaseCheckpoints();
    resetToStart(pl);
}

void onPathFound(PlayLayer* pl) {
    s.pathEndTick = curTick(pl);
    if (s.cfg.verify) startVerification(pl);
    else finish(pl, true, "", -1);
}

// ---------------- search ----------------

void backtrack(PlayLayer* pl);

void pushNode(PlayLayer* pl) {
    uint64_t h = hashGameState(pl, s.held);
    if (s.dead.count(h)) {
        backtrack(pl);
        return;
    }

    Node n{curTick(pl), pl->createCheckpoint(), h, s.held, false, 0, s.inputs.size(), s.path.size()};
    if (n.cp) n.cp->retain();
    else log::warn("Pathfinder: createCheckpoint returned null at tick {}", n.tick);

    n.first = chooseFirstInput(pl);
    s.stack.push_back(n);

    while (s.stack.size() - s.firstLive > s.cfg.maxCheckpoints) {
        auto& old = s.stack[s.firstLive];
        if (old.cp) old.cp->release();
        old.cp = nullptr;
        s.firstLive++;
    }

    setHeld(pl, n.first);
}

void backtrack(PlayLayer* pl) {
    s.backtracks++;
    uint32_t deathTick = curTick(pl);
    float deathPercent = pl->getCurrentPercent();

    s.deaths.push_back(overlay::playerPos(pl));
    if (s.deaths.size() > MAX_DEATH_MARKS) s.deaths.erase(s.deaths.begin());

    if (s.dead.size() > MAX_DEAD_STATES) s.dead.clear();
    while (!s.stack.empty() && s.stack.back().tried >= 1) {
        auto& b = s.stack.back();
        s.dead.insert(b.hash);
        if (b.cp) b.cp->release();
        s.stack.pop_back();
    }
    if (s.firstLive > s.stack.size()) s.firstLive = s.stack.size();

    if (s.stack.empty()) {
        finish(pl, false, "", -1);
        return;
    }

    auto& n = s.stack.back();
    n.tried = 1;
    if (static_cast<int>(deathTick - n.tick) > s.cfg.deadEndTicks) addDeadEnd(deathPercent);

    s.inputs.resize(n.inputsSize);
    if (s.path.size() > n.pathSize) s.path.resize(n.pathSize);

    if (n.cp) {
        pl->loadFromCheckpoint(n.cp);
        s.levelEnded = false;
        restoreButtonState(pl, n.heldBefore);
        setHeld(pl, !n.first);
    } else {
        // checkpoint was dropped: replay from the start up to this node
        s.phase = Phase::Replaying;
        s.replayTarget = n.tick;
        s.replayPos = 0;
        s.levelEnded = false;
        resetToStart(pl);
    }
}

// apply recorded inputs whose tick has come (replay / verification)
void applyScheduledInputs(PlayLayer* pl) {
    while (s.replayPos < s.inputs.size() && s.inputs[s.replayPos].tick <= curTick(pl)) {
        auto const& in = s.inputs[s.replayPos];
        sendButton(pl, in.button, in.down);
        if (in.button == 1) s.held = in.down;
        s.replayPos++;
    }
}

void searchStep(PlayLayer* pl, StepFn const& step) {
    stepOnce(pl, step);
    if (s.phase != Phase::Searching) return;

    if (reachedEnd(pl)) {
        recordPosition(pl);
        onPathFound(pl);
        return;
    }
    if (s.died) {
        backtrack(pl);
        return;
    }

    recordPosition(pl);
    float pct = pl->getCurrentPercent();
    if (pct > s.bestPercent) s.bestPercent = pct;
    if (!s.stack.empty() && curTick(pl) >= s.stack.back().tick + static_cast<uint32_t>(s.cfg.ticksPerStep))
        pushNode(pl);
}

void replayStep(PlayLayer* pl, StepFn const& step) {
    if (curTick(pl) >= s.replayTarget) {
        if (curTick(pl) != s.replayTarget) {
            finish(pl, false, "Replay desync (tick mismatch).", -1);
            return;
        }
        applyScheduledInputs(pl);
        auto& n = s.stack.back();
        if (s.held != n.heldBefore) {
            finish(pl, false, "Replay desync (button state mismatch).", -1);
            return;
        }
        n.cp = pl->createCheckpoint();
        if (n.cp) n.cp->retain();
        s.firstLive = s.stack.size() - 1;
        s.phase = Phase::Searching;
        setHeld(pl, !n.first);
        return;
    }

    applyScheduledInputs(pl);
    stepOnce(pl, step);
    if (s.died || reachedEnd(pl)) finish(pl, false, "Replay desync (the level is not deterministic).", -1);
}

void verifyStep(PlayLayer* pl, StepFn const& step) {
    applyScheduledInputs(pl);
    stepOnce(pl, step);
    if (reachedEnd(pl)) {
        finish(pl, true, "", 1);
        return;
    }
    if (s.died || curTick(pl) > s.verifyDeadline) {
        float pct = pl->getCurrentPercent();
        finish(pl, true, "", 0, pct);
    }
}

void start(PlayLayer* pl) {
    log::info("Pathfinder: starting");
    releaseCheckpoints();
    auto cfg = Settings::load();
    s = State{};
    s.cfg = cfg;
    s.startedAt = std::chrono::steady_clock::now();
    s.platformer = pl->m_isPlatformer;
    s.fromStartPos = pl->m_startPosObject != nullptr;

    s.enabledPractice = !pl->m_isPracticeMode;
    if (s.enabledPractice) pl->togglePracticeMode(true);
    pl->removeAllCheckpoints(); // otherwise resetLevel would respawn at a practice checkpoint
    resetToStart(pl);

    if (s.platformer) {
        // platformer: always walk right, search only the jump button
        sendButton(pl, 3, true);
        s.inputs.push_back({curTick(pl), 3, false, true});
    }

    overlay::ensure(pl);
    s.phase = Phase::Searching;
    recordPosition(pl);
    pushNode(pl);
}

void updateHud(PlayLayer* pl) {
    float pct = pl->getCurrentPercent();
    char const* phase = s.phase == Phase::Replaying ? " (replaying)" : s.phase == Phase::Verifying ? " (verifying)" : "";
    overlay::setStatus(fmt::format(
        "Pathfinding{}: {:.1f}% (best {:.1f}%)  {}\nTick {}  Backtracks: {}  Dead ends: {}  {:.0f}s",
        phase, pct, s.bestPercent, gamemodeName(detectGamemode(pl->m_player1)), curTick(pl), s.backtracks,
        s.deadEnds.size(), elapsedSec()));
    overlay::drawPath(s.path, s.deaths, s.cfg.drawPath);

    if (s.phase == Phase::Searching && s.cfg.drawPrediction) traj::drawLive(pl, s.cfg.predictionTicks);
    else traj::clearDrawing();
}

} // namespace

bool isActive() { return s.phase != Phase::Idle || s.startRequested; }
bool blocksInput() { return isActive() && !s.botInput; }

void requestStart() {
    s.startRequested = true;
    s.stopRequested = false;
}

void requestStop() { s.stopRequested = true; }

std::string statusText() {
    return fmt::format("Pathfinding is running.\nBest: {:.1f}%  Backtracks: {}\nDead ends: {}",
        s.bestPercent, s.backtracks, formatDeadEnds());
}

void onFrame(PlayLayer* pl, StepFn const& step) {
    if (s.startRequested) {
        s.startRequested = false;
        start(pl);
    }
    if (s.phase == Phase::Idle) return;
    if (s.stopRequested) {
        s.stopRequested = false;
        finish(pl, false, "Stopped by user.", -1);
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    while (s.phase != Phase::Idle) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (elapsed >= s.cfg.budgetMs) break;

        switch (s.phase) {
            case Phase::Searching: searchStep(pl, step); break;
            case Phase::Replaying: replayStep(pl, step); break;
            case Phase::Verifying: verifyStep(pl, step); break;
            case Phase::Idle: break;
        }
    }

    if (s.phase != Phase::Idle) updateHud(pl);
}

void onDeath() { s.died = true; }
void onLevelEnd() { s.levelEnded = true; }

void onQuit() {
    releaseCheckpoints();
    s = State{};
    overlay::onQuit();
}

} // namespace pf::search
