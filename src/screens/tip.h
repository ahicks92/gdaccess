#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// Tutorial tips as a mod-owned overlay: when the game shows a tip popup (it fetches `tagQuickTip*` and draws
// the tip top-left), this screen takes over -- it reads the tip's lines as items you can arrow through and
// closes the game's popup (a right click on it) on Close / Escape. Layer 35, modal.
std::unique_ptr<gd::core::Screen> make_tip();
}  // namespace gd::screens
