#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The quest-reward popup (title, quest name, experience line, Accept) and the shrine window (title, info,
// the three offering lines, Offer / Cancel / Close): InGameUI windows read by offset, buttons pressed through
// each window's own listener registry (docs/ingame-ui-survey.md; registries read out of the ctors 2026-08-22).
std::unique_ptr<gd::core::Screen> make_quest_reward();
std::unique_ptr<gd::core::Screen> make_shrine();
}  // namespace gd::screens
