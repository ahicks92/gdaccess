#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The codex window (Q): quests in progress / completed (tree: quest -> tasks -> objectives, rewards; Enter
// toggles tracking) and the lore codex (Enter reads a note). Model: src/gameapi.h (Quest2Repository, lore ids).
std::unique_ptr<gd::core::Screen> make_codex();
// The HUD's objective tracker lines, spoken (the Q key in the world).
void speak_objectives();
}  // namespace gd::screens
