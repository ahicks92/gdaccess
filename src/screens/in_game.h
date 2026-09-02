#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The world: the game keeps its keyboard (WASD through the game's own movement option); we add wall tones
// from navmesh probes and the InGame actions (where am I, ...). Active whenever the main player exists and no
// modelled menu is on top.
std::unique_ptr<gd::core::Screen> make_in_game();
void speak_where();  // the "where am I" action: position, life, region
// The wall-tone provider, ticked by the in-game screen; exposed for the dev server.
namespace walltones {
void set_enabled(bool on);
bool enabled();
void set_range(float units);  // wotr default 15 ft = 4.57 units
void set_gain(float gain);    // 0..1
void set_trim(int bank, int dir, float db);   // loudness trims: bank 1|2 + dir 0..3 (north east south west) in dB; bank 0 = all off, -1 = defaults
float trim_gain(int bank, int dir);   // the current loudness trim as a linear gain (bank 1|2, dir 0..3), for the sound glossary
std::string status();
std::string probe_timing(int iters);  // dev: time one tick's navmesh probing (4 free_distance rays + classify)
}  // namespace walltones
}  // namespace gd::screens
