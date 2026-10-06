#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// Search overlay: found path (green), deaths (red) and the status label.
namespace pf::overlay {

void ensure(PlayLayer* pl);
void drawPath(std::vector<cocos2d::CCPoint> const& path, std::vector<cocos2d::CCPoint> const& deaths, bool enabled);
void setStatus(std::string const& text);
void removeLabel();
void onQuit();

// player position in object-layer space
cocos2d::CCPoint playerPos(PlayLayer* pl);

} // namespace pf::overlay
