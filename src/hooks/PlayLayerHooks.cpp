#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "core/Search.hpp"
#include "sim/Trajectory.hpp"

using namespace geode::prelude;

class $modify(PFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // drop anything left from a previous PlayLayer
        pf::search::onQuit();
        pf::traj::onQuit();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (pf::traj::isFakePlayer(player)) {
            if (pf::traj::isSimulating()) pf::traj::markSimDeath();
            return; // the prediction player never really dies
        }
        if (pf::search::isActive()) {
            if (obj != m_anticheatSpike) pf::search::onDeath();
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        if (pf::search::isActive()) {
            pf::search::onLevelEnd();
            return;
        }
        PlayLayer::levelComplete();
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (pf::traj::isSimulating()) return;
        if (pf::search::isActive()) {
            pf::search::onLevelEnd();
            return;
        }
        PlayLayer::playEndAnimationToPos(pos);
    }

    void playPlatformerEndAnimationToPos(CCPoint pos, bool instant) {
        if (pf::traj::isSimulating()) return;
        if (pf::search::isActive()) {
            pf::search::onLevelEnd();
            return;
        }
        PlayLayer::playPlatformerEndAnimationToPos(pos, instant);
    }

    void onQuit() {
        pf::search::onQuit();
        pf::traj::onQuit();
        PlayLayer::onQuit();
    }
};
