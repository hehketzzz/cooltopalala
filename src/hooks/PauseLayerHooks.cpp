#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "core/Search.hpp"

using namespace geode::prelude;

class $modify(PFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto spr = ButtonSprite::create("PF");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PFPauseLayer::onPathfinder));
        btn->setID("pathfinder-button"_spr);

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto win = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->addChild(btn);
            menu2->setPosition({win.width - 35.f, win.height / 2.f});
            this->addChild(menu2);
        }
    }

    void onPathfinder(CCObject*) {
        if (pf::search::isActive()) {
            createQuickPopup("Pathfinder", pf::search::statusText(), "Close", "Stop",
                [](FLAlertLayer*, bool btn2) {
                    if (btn2) pf::search::requestStop();
                });
            return;
        }

        createQuickPopup(
            "Pathfinder",
            "Restarts the level and finds a path with the real game physics (all gamemodes, orbs, "
            "pads and portals work). Draws the path and the <cb>predicted trajectory</c>, reports dead "
            "ends and saves a <cy>240 TPS Mega Hack v9</c> macro (.gdr) into the Geometry Dash "
            "<cy>replays</c> folder.\n\nOpen this menu again to stop.",
            "Cancel", "Start pathfind",
            [this](FLAlertLayer*, bool btn2) {
                if (!btn2) return;
                pf::search::requestStart();
                this->onResume(nullptr);
            });
    }
};
