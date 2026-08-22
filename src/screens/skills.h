#pragma once
#include <memory>
#include <string>
#include "core/screen.h"

namespace gd::screens {
// The skill window (N): the skill-points line, then one row per skill of the UI list ("name, level 3 of
// 12[, locked][, mastery]"). Enter = spend a point, Backspace = refund one, Space = the game's skill text.
// Ctrl+1..0 / Ctrl+J / Ctrl+I assign the focused skill to a quickbar slot / left mouse / right mouse
// (screens/quickbar.h). Model: src/gameapi.h.
std::unique_ptr<gd::core::Screen> make_skills();

// A screen whose focused row can be assigned to the quickbar (the skills window; items later).
struct AssignSource {
  virtual ~AssignSource() = default;
  virtual unsigned focused_skill_id() = 0;   // 0 = nothing assignable focused
  virtual std::string focused_label() = 0;
};
}  // namespace gd::screens
