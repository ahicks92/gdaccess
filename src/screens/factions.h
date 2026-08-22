#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The factions window (J): one row per known faction -- name, standing, progress within the tier -- from the
// player's FactionPack through src/gameapi.h. Read-only.
std::unique_ptr<gd::core::Screen> make_factions();
}  // namespace gd::screens
