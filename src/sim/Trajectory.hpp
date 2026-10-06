#pragma once
#include <Geode/Geode.hpp>
#include <vector>

// Trajectory prediction.
//
// Same technique as the "Show Trajectory" hacks (Eclipse / Mega Hack): an invisible copy of
// the player is created, the real player's state is copied into it (position, velocity,
// gamemode, size, gravity, speed) and then it is stepped tick by tick with the game's own
// PlayerObject::update + GJBaseGameLayer::checkCollisions. That way every gamemode
// (cube, ship, ball, UFO, wave, robot, spider, swing, mini, mirror, upside down,
// platformer) moves exactly like in the game.
//
// While simulating, everything that would change the level is suppressed (triggers, orbs,
// pads, portals, coins, effects), so only solids / hazards / slopes are considered.

namespace pf::traj {

struct Prediction {
    std::vector<cocos2d::CCPoint> points; // object-layer positions, one per tick
    int survivedTicks = 0;
    bool died = false;
};

bool isSimulating();
bool isFakePlayer(PlayerObject* p);
void markSimDeath();
void setTickDelta(float dt); // per-tick delta that the game passes to PlayerObject::update

// Predict `ticks` ticks of `base` with the jump button held or released.
Prediction predict(PlayLayer* pl, PlayerObject* base, bool hold, int ticks, bool collectPoints);

// Predict hold + release for both players and draw it.
void drawLive(PlayLayer* pl, int ticks);
void clearDrawing();

void onQuit();

} // namespace pf::traj
