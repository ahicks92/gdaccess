#pragma once
// The game's transient on-screen messages, spoken through the screen reader (docs/combat-feedback.md):
//  - the HUD notification banner strip (GameEngine::AddUINotification): level up, "Enemy Hero Killed", quest
//    updates, "You have died" -- already localized u16 text.
//  - the red action-failed popup (ControllerPlayer::SetUserText): "That skill is not ready", "Energy Too Low",
//    "Invalid Target" -- a localization tag we localize.
// This hook is the SINGLE source for these banners; nothing else synthesizes level-up / kill / XP lines, so
// there is no double announcement. Deduped against the immediately-preceding identical line (the game re-sets
// the popup while it is showing). Game-thread only except status().
#include <string>
namespace gd::notify {
bool install();
void remove();
std::string status();   // /notify
}  // namespace gd::notify
