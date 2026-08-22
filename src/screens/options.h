#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The Options screen (app state 5): tabs, the active page's check boxes / sliders / drop-downs / key-binding
// table, and Apply / Default / Close -- all read from and driven through the exe's widgets. Layer 0 (it
// replaces the main menu while it is up).
std::unique_ptr<gd::core::Screen> make_options();
}  // namespace gd::screens
