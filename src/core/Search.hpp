#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// Depth-first path search with backtracking on top of the real game physics.
//
//  * Every decision step (N physics ticks) a checkpoint is saved and an input is chosen.
//    The first choice comes from the trajectory prediction for the current gamemode
//    (the input that survives longer), the second one is tried only after a death.
//  * Proven deadly states are hashed and never retried.
//  * Old checkpoints are dropped to save memory; reaching them again replays the inputs
//    from the start of the level.
//  * The found path is verified by replaying it from the start exactly like a bot does,
//    then exported as a 240 TPS GDR macro for Mega Hack v9.

namespace pf::search {

using StepFn = std::function<void(float)>; // calls the original GJBaseGameLayer::update

bool isActive();          // a search / replay / verification is running
bool blocksInput();       // real player input must be ignored right now
void requestStart();
void requestStop();
std::string statusText(); // for the pause menu

void onFrame(PlayLayer* pl, StepFn const& step);
void onDeath();
void onLevelEnd();
void onQuit();

} // namespace pf::search
