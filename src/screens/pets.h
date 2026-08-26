#pragma once
#include <memory>

namespace gd::core { class Screen; }

namespace gd::screens {
// Pets (docs/pets.md). The overlay (Backspace in the world): one row per pet -- Left/Right = stance (per summoning
// skill), Enter = toggle selected, Backspace = disband, Space = where it is -- then the command rows (the selected
// pets, or all when none are selected: attack the locked target / recall). F2..F6 / F7 = the same selection from
// the world (announced); Shift+Backspace = the attack command from the world.
void open_pet_overlay();
void toggle_pet_selected(int index);   // 0-based, list order (= the game's F2..F6 order)
void select_all_pets();
void pets_attack_locked();             // the selected (or all) pets attack the review lock's target; clears the selection
// Per-frame from the in-world tick: "<pet> summoned" / "<pet> down" in Zira from the pet list's membership.
void pets_tick();
void pets_reset();
std::unique_ptr<gd::core::Screen> make_pet_overlay();
}  // namespace gd::screens
