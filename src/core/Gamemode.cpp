#include "Gamemode.hpp"
#include <Geode/Geode.hpp>

namespace pf {

Gamemode detectGamemode(PlayerObject* p) {
    if (!p) return Gamemode::Cube;
    if (p->m_isShip) return Gamemode::Ship;
    if (p->m_isBird) return Gamemode::Ufo;
    if (p->m_isBall) return Gamemode::Ball;
    if (p->m_isDart) return Gamemode::Wave;
    if (p->m_isRobot) return Gamemode::Robot;
    if (p->m_isSpider) return Gamemode::Spider;
    if (p->m_isSwing) return Gamemode::Swing;
    return Gamemode::Cube;
}

char const* gamemodeName(Gamemode mode) {
    switch (mode) {
        case Gamemode::Cube: return "Cube";
        case Gamemode::Ship: return "Ship";
        case Gamemode::Ball: return "Ball";
        case Gamemode::Ufo: return "UFO";
        case Gamemode::Wave: return "Wave";
        case Gamemode::Robot: return "Robot";
        case Gamemode::Spider: return "Spider";
        case Gamemode::Swing: return "Swing";
    }
    return "?";
}

bool isContinuousMode(Gamemode mode) {
    return mode == Gamemode::Ship || mode == Gamemode::Wave || mode == Gamemode::Swing;
}

void applyGamemode(PlayerObject* p, Gamemode mode) {
    if (!p || detectGamemode(p) == mode) return;
    // leave the current mode first
    if (p->m_isShip) p->toggleFlyMode(false, true);
    if (p->m_isBird) p->toggleBirdMode(false, true);
    if (p->m_isBall) p->toggleRollMode(false, true);
    if (p->m_isDart) p->toggleDartMode(false, true);
    if (p->m_isRobot) p->toggleRobotMode(false, true);
    if (p->m_isSpider) p->toggleSpiderMode(false, true);
    if (p->m_isSwing) p->toggleSwingMode(false, true);

    switch (mode) {
        case Gamemode::Cube: break;
        case Gamemode::Ship: p->toggleFlyMode(true, true); break;
        case Gamemode::Ufo: p->toggleBirdMode(true, true); break;
        case Gamemode::Ball: p->toggleRollMode(true, true); break;
        case Gamemode::Wave: p->toggleDartMode(true, true); break;
        case Gamemode::Robot: p->toggleRobotMode(true, true); break;
        case Gamemode::Spider: p->toggleSpiderMode(true, true); break;
        case Gamemode::Swing: p->toggleSwingMode(true, true); break;
    }
}

} // namespace pf
