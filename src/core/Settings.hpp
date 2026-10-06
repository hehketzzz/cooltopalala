#pragma once
#include <cstddef>
#include <filesystem>
#include <string>

namespace pf {

static constexpr double TPS = 240.0; // GD 2.2 physics ticks per second

struct Settings {
    int ticksPerStep = 4;
    int budgetMs = 14;
    size_t maxCheckpoints = 3000;
    int deadEndTicks = 120;
    bool smartOrder = true;
    int lookaheadTicks = 60;
    bool verify = true;

    bool drawPath = true;
    bool drawPrediction = true;
    int predictionTicks = 240;

    std::filesystem::path exportDir;
    std::string exportFormat = "both";

    static Settings load();
};

// cheap per-frame lookups for the in-game trajectory
bool predictionInGameEnabled();
int predictionTicksSetting();

} // namespace pf
