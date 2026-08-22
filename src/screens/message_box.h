#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The game's generic message boxes: a few lines of text over an "Okay" button, or a question over Yes / No.
// Layer 30, modal. The message is the game's own text above the buttons (read verbatim); Enter activates the
// focused button, Escape dismisses (Okay / No).
std::unique_ptr<gd::core::Screen> make_message_box();
}  // namespace gd::screens
