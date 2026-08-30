#pragma once
// The loot filter window (Ctrl+O): four tab stops (Quality / Type / Damage / Character) of toggles that apply
// immediately, Space = the game's tooltip, the last row of each column = that column's defaults. docs/loot-filter.md.
#include <memory>
#include "core/screen.h"

namespace gd::screens {
std::unique_ptr<gd::core::Screen> make_loot_filter();
}
