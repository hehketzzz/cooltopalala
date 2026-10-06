#include "Overlay.hpp"

using namespace geode::prelude;

namespace pf::overlay {

namespace {
PlayLayer* s_owner = nullptr;
CCDrawNode* s_path = nullptr;
CCLabelBMFont* s_label = nullptr;
}

void ensure(PlayLayer* pl) {
    if (s_owner != pl) {
        // nodes of an old layer are owned (and freed) by that layer
        s_path = nullptr;
        s_label = nullptr;
        s_owner = pl;
    }
    if (!s_path && pl->m_objectLayer) {
        s_path = CCDrawNode::create();
        pl->m_objectLayer->addChild(s_path, 9999);
    }
    if (!s_label) {
        auto win = CCDirector::get()->getWinSize();
        s_label = CCLabelBMFont::create("Pathfinding...", "bigFont.fnt");
        s_label->setAnchorPoint({0.f, 1.f});
        s_label->setScale(0.32f);
        s_label->setPosition({5.f, win.height - 5.f});
        pl->addChild(s_label, 10000);
    }
}

void drawPath(std::vector<CCPoint> const& path, std::vector<CCPoint> const& deaths, bool enabled) {
    if (!s_path) return;
    s_path->clear();
    if (!enabled) return;

    ccColor4F green = {0.f, 1.f, 0.3f, 1.f};
    ccColor4F red = {1.f, 0.1f, 0.1f, 0.8f};

    for (size_t i = 1; i < path.size(); i++) {
        auto& a = path[i - 1];
        auto& b = path[i];
        if (std::abs(a.x - b.x) > 300.f || std::abs(a.y - b.y) > 300.f) continue; // teleports
        s_path->drawSegment(a, b, 1.2f, green);
    }
    for (auto& d : deaths) s_path->drawDot(d, 2.5f, red);
}

void setStatus(std::string const& text) {
    if (s_label) s_label->setString(text.c_str());
}

void removeLabel() {
    if (s_label) s_label->removeFromParent();
    s_label = nullptr;
}

void onQuit() {
    // destroyed together with the PlayLayer
    s_label = nullptr;
    s_path = nullptr;
    s_owner = nullptr;
}

CCPoint playerPos(PlayLayer* pl) {
    auto p = pl->m_player1;
    if (!p || !p->getParent() || !pl->m_objectLayer) return {0.f, 0.f};
    if (p->getParent() == pl->m_objectLayer) return p->getPosition();
    auto world = p->getParent()->convertToWorldSpace(p->getPosition());
    return pl->m_objectLayer->convertToNodeSpace(world);
}

} // namespace pf::overlay
