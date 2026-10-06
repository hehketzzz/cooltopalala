# Pathfinder 2.0

Geode mod for Geometry Dash 2.2081: finds a path through a level with the real game physics and exports a Mega Hack v9 macro.

## Structure

```
src/
  main.cpp                 entry point
  core/Search.*            DFS search with checkpoints, replays and verification
  core/Settings.*          mod settings
  core/Gamemode.*          gamemode detection (cube/ship/ball/ufo/wave/robot/spider/swing)
  core/StateHash.*         physics-state hash (deadly states are not retried)
  sim/Trajectory.*         trajectory prediction with the game's own physics
  macro/Macro.*            GDR export (.gdr msgpack + .gdr.json)
  ui/Overlay.*             path, deaths, status label
  hooks/*.cpp              Geode hooks
```

## How it works

* Every decision step (by default 4 ticks = one 60 FPS frame) the bot saves a checkpoint and picks an input.
* **Smart order:** before each decision, the current gamemode is read and the trajectory is
  predicted for "hold" and "release" (Show Trajectory technique: an invisible copy of the player is
  stepped with `PlayerObject::update` + `checkCollisions`). The input that survives longer is tried
  first. Default: ship/wave/swing keep the current input, all other modes stay released.
* On death it backtracks to the last checkpoint and tries the other input.
* The found path is **verified** by replaying it from the start, exactly like a bot plays a macro.
* The macro is saved at **240 TPS** (frames = `m_currentProgress`, the same convention as Mega Hack / Eclipse / xdBot) to `<GD folder>/replays` as `.gdr` and `.gdr.json`. A backup copy goes to the mod save folder.

## Drawing

* green: found path, red dots: deaths
* blue: where the player goes if the button is held, orange: if it is released, red cross: death in the prediction

## Limitations

* Platformer: the bot holds right and searches only the jump.
* 2-player levels: only player 1's inputs are searched.
* If verification fails, the level is non-deterministic or the prediction affects the state; try turning off "Smart input order".
