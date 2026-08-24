#pragma once
// The sonar field (src/core/sonar_field.h): the living enemies, the loot and the dungeon entrances around the
// player each repeat their OWN tone (assets/audio/interactables: units-enemy, unknown, transition), the period
// shrinking as the thing nears (logarithmically in distance, 0.14s@2u..0.80s@25u) and its left/right offsetting the phase
// so co-distant things stagger; volume/pan/rear-shelf are the shared ear frame. Replaces the old left-to-right
// sweep, which read as noise with a fixed viewpoint and moving enemies. Automatic while in the world; /sonar knobs.
#include <string>

namespace gd::sonar {
void tick();                 // per frame from the in-game screen
void reset();                // leaving the world
void set_enabled(bool on);
bool enabled();
void set_knob(const std::string& name, float value);   // radius vol ref floor pnear pfar dnear dfar force
std::string status();
}  // namespace gd::sonar
