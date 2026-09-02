#pragma once
// The T overlay (2026-09-01): which combat announcements the player gets. Three toggles, persisted in
// settings.txt: outgoing (your hits, kills and XP in Mark), incoming (your health steps and effects on you in
// Zira), telegraph cues (the swing / stomp / wave / shot / ring words at an enemy's cast start).
#include <memory>
namespace gd::core { class Screen; }
namespace gd::screens {
void open_announcements();
std::unique_ptr<gd::core::Screen> make_announcements_overlay();
}
