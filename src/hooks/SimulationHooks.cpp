// Suppress side effects of the invisible prediction player (sounds, particles, triggers,
// jump counters, trails). Based on the approach used by Eclipse Menu's Show Trajectory.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include "sim/Trajectory.hpp"

using namespace geode::prelude;

class $modify(PFSimPlayer, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (pf::traj::isSimulating()) return;
        auto pl = PlayLayer::get();
        if (pl && this == pl->m_player1) pf::traj::setTickDelta(dt);
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (pf::traj::isSimulating()) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (pf::traj::isSimulating()) return;
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (pf::traj::isSimulating()) return;
        PlayerObject::ringJump(ring, skipCheck);
    }
};

class $modify(PFSimGameObject, GameObject) {
    void playShineEffect() {
        if (pf::traj::isSimulating()) return;
        GameObject::playShineEffect();
    }
};

class $modify(PFSimEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (pf::traj::isSimulating()) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(PFSimStreak, HardStreak) {
    void addPoint(CCPoint point) {
        if (pf::traj::isSimulating()) return;
        HardStreak::addPoint(point);
    }
};
