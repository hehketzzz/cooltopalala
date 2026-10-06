#include "StateHash.hpp"
#include "Gamemode.hpp"
#include <Geode/Geode.hpp>

namespace pf {

static uint64_t hashPlayer(uint64_t h, PlayerObject* p) {
    if (!p) return h;
    h = hashMix(h, bitsOf(p->getPositionX()));
    h = hashMix(h, bitsOf(p->getPositionY()));
    h = hashMix(h, bitsOf(p->getRotation()));
    h = hashMix(h, bitsOf(p->m_yVelocity));
    h = hashMix(h, bitsOf(p->m_vehicleSize));
    h = hashMix(h, bitsOf(p->m_playerSpeed));
    h = hashMix(h, bitsOf(p->m_gravityMod));
    h = hashMix(h, static_cast<uint64_t>(detectGamemode(p)));
    uint64_t flags =
        (uint64_t(p->m_isUpsideDown) << 0) | (uint64_t(p->m_isOnGround) << 1) |
        (uint64_t(p->m_isSideways) << 2) | (uint64_t(p->m_isGoingLeft) << 3) |
        (uint64_t(p->m_isDashing) << 4) | (uint64_t(p->m_isOnSlope) << 5);
    return hashMix(h, flags);
}

uint64_t hashGameState(PlayLayer* pl, bool held) {
    uint64_t h = 1469598103934665603ULL;
    h = hashMix(h, uint64_t(pl->m_gameState.m_currentProgress));
    h = hashMix(h, uint64_t(held));
    h = hashPlayer(h, pl->m_player1);
    if (pl->m_gameState.m_isDualMode) h = hashPlayer(h, pl->m_player2);
    return h;
}

} // namespace pf
