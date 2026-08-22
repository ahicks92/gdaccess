#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The main menu's "Delete Character" dialog: the game's prompt, an edit box that must read "DELETE", Accept
// (enabled by the game once it does) and Cancel. Read from the window's widgets. Layer 20 over the main menu.
std::unique_ptr<gd::core::Screen> make_delete_character();
}  // namespace gd::screens
