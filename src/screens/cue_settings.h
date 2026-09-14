#pragma once
// The F8 overlay: which positioned sound cues play and how loud each channel is (src/cues.h). Modelled on
// the T overlay: two Tab stops (cues on/off; channel volumes in percent, Left/Right 10, Enter steps up and wraps),
// a game window covers and closes it, every change is saved at once.
#include <memory>
#include "core/screen.h"

namespace gd::screens {
void open_cue_settings();
std::unique_ptr<gd::core::Screen> make_cue_settings_overlay();
}  // namespace gd::screens
