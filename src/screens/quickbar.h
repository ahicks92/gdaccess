#pragma once
#include <string>

namespace gd::screens {
// The quickbar without a window (docs/controls.md). Bare Y is the game's own Quickbar Switch (passed straight
// through); quickbar_tick() announces the page when it changes. Reading is per slot: Ctrl+1..0 read slot
// 1..10 of the displayed bar, Ctrl+- / Ctrl+= read the left / right mouse skill -- each says the skill name
// and, from world::skill_aim, how it aims (self / around you / at a spot / at a target). In the skills window
// (AssignSource) Ctrl+1..0 instead assign the focused skill to a slot, Ctrl+J / Ctrl+I to the mouse buttons.
// Model: src/gameapi.h hot slots.
void speak_slot(int k);            // read quickbar slot k (1..10) of the displayed bar
void speak_mouse(bool primary);    // read the left (primary) / right (secondary) mouse skill
void quickbar_tick();              // per world frame: announce a page change
void quickbar_reset();             // forget the last page (on leaving the world), so the next switch announces
// Assign the current screen's focused skill: target 1..10 = quickbar slot, 0 = left mouse, -1 = right mouse.
void assign_focused(int target);
// The game's own Pickup action (the nearest item on the ground; G in the world).
void pickup_nearest();
// The hot-slot index of quickbar slot k (1..10) on the bar the HUD shows.
unsigned quickbar_slot_index(int k);
void set_quickbar_base(unsigned base, unsigned stride);
}  // namespace gd::screens
