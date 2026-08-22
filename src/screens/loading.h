#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The loading screen between the menu and the world: the game draws a "Tip: ..." line and no HUD. Speaks
// "loading" and the tip once; nothing to navigate. Layer 40 so it covers whatever was below.
std::unique_ptr<gd::core::Screen> make_loading();
}  // namespace gd::screens
