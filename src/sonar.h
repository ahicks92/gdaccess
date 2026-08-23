#pragma once
// The sonar sweep (wotr-access SonarSystem, ported): every sweep, the living enemies, the loot and the dungeon
// entrances around the player are pinged once each, left to right, with their category's cue
// (assets/audio/interactables: units-enemy, unknown, transition), at an inverse-distance volume, panned in the
// ear frame and darkened by the rear shelf when behind. Automatic while in the world; dev knobs on /sonar.
#include <string>

namespace gd::sonar {
void tick();                 // per frame from the in-game screen
void reset();                // leaving the world
void set_enabled(bool on);
bool enabled();
void set_knob(const std::string& name, float value);   // radius vol ref gap_min gap_max rest
std::string status();
}  // namespace gd::sonar
