#pragma once
#include <memory>

namespace gd::core { class Screen; }

namespace gd::screens {
// The hotbar manager (Ctrl+`): lists the current weapon set's two number bars; activating a slot opens a
// skill picker (clear + every assignable skill, incl. item-granted) for that slot. Escape closes it.
void open_hotbar_manager();
// Swap the active weapon set (F) and announce the new set + its two hands.
void swap_weapons();
// Per-frame from the in-world tick: emits the deferred weapon-swap announcement once the swap has propagated.
void weapon_swap_tick();
std::unique_ptr<gd::core::Screen> make_hotbar_manager();
}  // namespace gd::screens
