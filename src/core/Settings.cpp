#include "Settings.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace pf {

static int64_t intSetting(char const* key) { return Mod::get()->getSettingValue<int64_t>(key); }
static bool boolSetting(char const* key) { return Mod::get()->getSettingValue<bool>(key); }

Settings Settings::load() {
    Settings s;
    s.ticksPerStep = static_cast<int>(intSetting("ticks-per-step"));
    s.budgetMs = static_cast<int>(intSetting("budget"));
    s.maxCheckpoints = static_cast<size_t>(intSetting("max-checkpoints"));
    s.deadEndTicks = static_cast<int>(intSetting("dead-end-ticks"));
    s.smartOrder = boolSetting("smart-order");
    s.lookaheadTicks = static_cast<int>(intSetting("lookahead-ticks"));
    s.verify = boolSetting("verify");
    s.drawPath = boolSetting("trajectory");
    s.drawPrediction = boolSetting("draw-prediction");
    s.predictionTicks = static_cast<int>(intSetting("prediction-ticks"));

    s.exportDir = Mod::get()->getSettingValue<std::filesystem::path>("export-dir");
    if (s.exportDir.empty()) s.exportDir = dirs::getGameDir() / "replays";
    s.exportFormat = Mod::get()->getSettingValue<std::string>("export-format");
    return s;
}

bool predictionInGameEnabled() { return boolSetting("prediction-in-game"); }
int predictionTicksSetting() { return static_cast<int>(intSetting("prediction-ticks")); }

} // namespace pf
