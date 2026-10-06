// Pathfinder entry point. The mod is split into modules:
//   core/   - search (DFS with checkpoints), settings, gamemode detection, state hashing
//   sim/    - trajectory prediction with the game's own physics
//   macro/  - GDR (Mega Hack v9) macro export
//   ui/     - overlay drawing and status label
//   hooks/  - Geode hooks that connect everything to the game
#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("Pathfinder loaded");
}
