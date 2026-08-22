#pragma once
// The mod proper: owns the input manager, the screen stack, focus mode and the navigator, and ticks them
// once per game frame (from Display::Update, on the game thread). Everything below src/core is engine-free;
// this is where it meets the game.
#include <string>
#include <string_view>
#include "core/input.h"
#include "core/screen.h"

namespace gd::core { class GraphNavigator; }

namespace gd::app {
void init();   // after hooks are installed (init thread)
void tick();   // once per frame, game thread
void shutdown();

bool owns_keyboard();         // true: the current screen takes the keyboard (game keys muted); decided by the screen stack
gd::core::InputManager& input();
gd::core::ScreenManager& screens();
gd::core::GraphNavigator* navigator();
double now();                 // seconds, monotonic

// dev server hooks
bool fire_action(std::string_view action_key);  // fire an action exactly as a real press would (game thread)
std::string action_keys();
std::string gui_dump();
}  // namespace gd::app
