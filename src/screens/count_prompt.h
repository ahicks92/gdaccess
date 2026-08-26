#pragma once
#include <functional>
#include <memory>
#include <string>
#include "core/screen.h"

namespace gd::screens {

// A mod-owned "how many?" prompt: a layered overlay above the screen that opened it (like the list picker).
// The title is spoken on entry ("sell how many of 12"); digits typed are echoed, Backspace deletes, Enter
// commits a value in 1..max (out of range: the range is spoken and the prompt stays), Escape cancels.
// Closing re-exposes the launching screen with its focus intact. Only one prompt at a time.
void open_count_prompt(std::string title, unsigned max, std::function<void(unsigned)> on_commit);
bool count_prompt_open();

std::unique_ptr<gd::core::Screen> make_count_prompt();

}  // namespace gd::screens
