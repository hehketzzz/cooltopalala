#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include "core/Search.hpp"
#include "core/Settings.hpp"
#include "sim/Trajectory.hpp"

using namespace geode::prelude;

class $modify(PFGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (pf::search::blocksInput()) return; // real input is ignored during the search
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }

        if (pf::search::isActive()) {
            pf::search::onFrame(pl, [this](float stepDt) { this->GJBaseGameLayer::update(stepDt); });
            return;
        }

        GJBaseGameLayer::update(dt);

        if (pf::predictionInGameEnabled()) pf::traj::drawLive(pl, pf::predictionTicksSetting());
        else pf::traj::clearDrawing();
    }

    // ---- trajectory simulation: only solids, hazards and slopes are collided with ----
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        if (!pf::traj::isSimulating()) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
            return;
        }
        gd::vector<GameObject*> filtered;
        for (int i = 0; i < count; i++) {
            auto obj = objects->at(i);
            if (obj->m_objectType == GameObjectType::Solid || obj->m_objectType == GameObjectType::Hazard ||
                obj->m_objectType == GameObjectType::AnimatedHazard || obj->m_objectType == GameObjectType::Slope) {
                filtered.push_back(obj);
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &filtered, static_cast<int>(filtered.size()), dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* obj) {
        if (pf::traj::isSimulating()) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(player, obj);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (pf::traj::isSimulating()) return;
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (pf::traj::isSimulating()) return;
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }
};
