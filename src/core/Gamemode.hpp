#pragma once

class PlayerObject;

namespace pf {

enum class Gamemode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

Gamemode detectGamemode(PlayerObject* player);
char const* gamemodeName(Gamemode mode);

// Modes where holding the button is continuous control (ship, wave, swing):
// the natural default is to keep the current input. In the other modes a click
// is a discrete action, so the natural default is "not pressed".
bool isContinuousMode(Gamemode mode);

// Switches a (simulation) player into the given gamemode without effects.
void applyGamemode(PlayerObject* player, Gamemode mode);

} // namespace pf
