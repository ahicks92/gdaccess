#pragma once
#include <string>

namespace gd::screens {
// The quickbar without a window: Y reads the displayed bar (slot 1..10 with what it holds, then the mouse
// slots); in a window whose focused row is assignable (screens/skills.h AssignSource), Ctrl+1..0 put it on
// slot 1..10 and Ctrl+J / Ctrl+I on the left / right mouse button. Model: src/gameapi.h hot slots.
void speak_quickbar();
// Assign the current screen's focused skill: target 1..10 = quickbar slot, 0 = left mouse, -1 = right mouse.
void assign_focused(int target);
// The game's own Pickup action (the nearest item on the ground; G in the world).
void pickup_nearest();
// The hot-slot index of quickbar slot k (1..10) on the bar the HUD shows.
unsigned quickbar_slot_index(int k);
void set_quickbar_base(unsigned base, unsigned stride);
}  // namespace gd::screens
