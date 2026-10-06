#include "Trajectory.hpp"
#include "core/Gamemode.hpp"

using namespace geode::prelude;

namespace pf::traj {

namespace {
PlayLayer* s_owner = nullptr; // layer the nodes below belong to
PlayerObject* s_fake = nullptr;
CCDrawNode* s_draw = nullptr;
bool s_simulating = false;
bool s_dead = false;
float s_tickDt = 0.25f; // overwritten with the real value as soon as the game updates

void releaseNodes() {
    if (s_fake) s_fake->release();
    if (s_draw) s_draw->release();
    s_fake = nullptr;
    s_draw = nullptr;
    s_owner = nullptr;
}

void checkOwner(PlayLayer* pl) {
    if (s_owner != pl) {
        releaseNodes();
        s_owner = pl;
    }
}

PlayerObject* ensureFake(PlayLayer* pl) {
    checkOwner(pl);
    if (s_fake) return s_fake;
    if (!pl || !pl->m_objectLayer) return nullptr;
    s_fake = PlayerObject::create(1, 1, pl, pl, true);
    if (!s_fake) return nullptr;
    s_fake->retain();
    s_fake->setPosition({0.f, 105.f});
    s_fake->setVisible(false);
    pl->m_objectLayer->addChild(s_fake);
    return s_fake;
}

CCDrawNode* ensureDraw(PlayLayer* pl) {
    checkOwner(pl);
    if (s_draw) return s_draw;
    if (!pl || !pl->m_objectLayer) return nullptr;
    s_draw = CCDrawNode::create();
    s_draw->retain();
    pl->m_objectLayer->addChild(s_draw, 9998);
    return s_draw;
}

void resetCollisionLog(PlayerObject* p) {
    if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
    if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
    if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
    if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
}

// Copy the physical state of the real player into the simulation player.
void copyState(PlayerObject* fake, PlayerObject* base) {
    fake->releaseButton(PlayerButton::Jump);
    fake->copyAttributes(base);
    // read the current gamemode and make sure the copy is really in it
    applyGamemode(fake, detectGamemode(base));

    fake->setPosition(base->getPosition());
    fake->setRotation(base->getRotation());
    fake->m_yVelocity = base->m_yVelocity;
    fake->m_gravityMod = base->m_gravityMod;
    fake->m_playerSpeed = base->m_playerSpeed;
    fake->m_isUpsideDown = base->m_isUpsideDown;
    fake->m_isSideways = base->m_isSideways;
    fake->m_isGoingLeft = base->m_isGoingLeft;
    fake->m_isOnGround = base->m_isOnGround;
    fake->m_isPlatformer = base->m_isPlatformer;
    fake->m_holdingRight = base->m_holdingRight;
    fake->m_holdingLeft = base->m_holdingLeft;
    fake->setVisible(false);
}
} // namespace

bool isSimulating() { return s_simulating; }
bool isFakePlayer(PlayerObject* p) { return p && p == s_fake; }
void markSimDeath() { s_dead = true; }
void setTickDelta(float dt) {
    if (dt > 0.f && dt < 10.f) s_tickDt = dt;
}

Prediction predict(PlayLayer* pl, PlayerObject* base, bool hold, int ticks, bool collectPoints) {
    Prediction out;
    if (!pl || !base || s_simulating) return out;
    auto fake = ensureFake(pl);
    if (!fake) return out;

    s_simulating = true;
    s_dead = false;
    copyState(fake, base);
    if (collectPoints) {
        out.points.reserve(ticks + 1);
        out.points.push_back(fake->getPosition());
    }

    for (int i = 0; i < ticks; i++) {
        resetCollisionLog(fake);
        pl->checkCollisions(fake, s_tickDt, false);
        if (s_dead) {
            out.died = true;
            break;
        }
        // the input is applied after the first collision pass, so ground state is known
        if (i == 0 && hold) fake->pushButton(PlayerButton::Jump);

        fake->update(s_tickDt);
        out.survivedTicks++;
        if (collectPoints) out.points.push_back(fake->getPosition());
    }

    fake->releaseButton(PlayerButton::Jump);
    s_simulating = false;
    return out;
}

static void drawPrediction(Prediction const& p, ccColor4F color) {
    for (size_t i = 1; i < p.points.size(); i++) {
        auto& a = p.points[i - 1];
        auto& b = p.points[i];
        if (std::abs(a.x - b.x) > 200.f || std::abs(a.y - b.y) > 200.f) continue;
        s_draw->drawSegment(a, b, 0.8f, color);
    }
    if (p.died && !p.points.empty()) {
        auto end = p.points.back();
        ccColor4F red = {1.f, 0.f, 0.f, 1.f};
        s_draw->drawSegment(end + CCPoint{-4.f, -4.f}, end + CCPoint{4.f, 4.f}, 1.f, red);
        s_draw->drawSegment(end + CCPoint{-4.f, 4.f}, end + CCPoint{4.f, -4.f}, 1.f, red);
    }
}

void drawLive(PlayLayer* pl, int ticks) {
    if (!ensureDraw(pl)) return;
    s_draw->clear();
    s_draw->setVisible(true);

    ccColor4F holdColor = {0.1f, 0.75f, 1.f, 0.9f};
    ccColor4F releaseColor = {1.f, 0.6f, 0.1f, 0.9f};

    PlayerObject* players[2] = {pl->m_player1, pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr};
    for (auto p : players) {
        if (!p) continue;
        drawPrediction(predict(pl, p, true, ticks, true), holdColor);
        drawPrediction(predict(pl, p, false, ticks, true), releaseColor);
    }
}

void clearDrawing() {
    if (s_draw) s_draw->clear();
}

void onQuit() {
    // both nodes are children of the object layer, which is destroyed with the PlayLayer
    releaseNodes();
    s_simulating = false;
}

} // namespace pf::traj
