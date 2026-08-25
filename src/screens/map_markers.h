#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The map picker (Ctrl+M opens the game's aerial map; this screen presents it accessibly): two tab stops --
// quest markers and points of interest -- listing the game's own map icons nearest-first. Activating a row
// sets it as the follow target (the ' key then pings it with distance and heading). See docs/controls.md.
std::unique_ptr<gd::core::Screen> make_map_markers();
}  // namespace gd::screens
