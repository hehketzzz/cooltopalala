#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pf::macro {

// One input event. `tick` is GJGameState::m_currentProgress at the moment the input was
// sent (same convention as Mega Hack / Eclipse / xdBot GDR recorders).
struct Input {
    uint32_t tick;
    int button; // 1 = jump, 2 = left, 3 = right
    bool player2;
    bool down;
};

struct Info {
    std::string levelName;
    int levelID = 0;
    bool platformer = false;
    double framerate = 240.0; // ticks per second
    uint32_t lastTick = 0;
};

struct ExportResult {
    std::vector<std::filesystem::path> files; // successfully written
    std::vector<std::string> errors;
};

// Writes a GDR v1 replay (.gdr = msgpack, .gdr.json = JSON) into `dir` and a backup copy
// into the mod save folder. `format` is "gdr", "gdr.json" or "both".
ExportResult exportMacro(Info const& info, std::vector<Input> const& inputs,
                         std::filesystem::path const& dir, std::string const& format);

std::string pathToUtf8(std::filesystem::path const& p);

} // namespace pf::macro
