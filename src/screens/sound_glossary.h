#pragma once
// The sound glossary (2026-09-01, F1 menu): every WAV the mod plays, as a tree by system -- wall tones, sonar cues,
// review pings, telegraph cues (labelled by the attack shape they announce). Landing on a row plays it the way
// the game would (pan, rear shelf); Enter does nothing. Opens anywhere, main menu included.
#include <memory>
namespace gd::core { class Screen; }
namespace gd::screens {
void open_sound_glossary();
std::unique_ptr<gd::core::Screen> make_sound_glossary();
}
