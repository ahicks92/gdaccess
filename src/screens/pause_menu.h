#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The in-game Escape menu: Return to Game / Options Menu / Exit to Main Menu / Quit to Desktop (measured
// 2026-08-21 at 1600x900: a centred column at x 809, y 375/427/478/529). Layer 25, modal; Escape = Return.
std::unique_ptr<gd::core::Screen> make_pause_menu();
}  // namespace gd::screens
