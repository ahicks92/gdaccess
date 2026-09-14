#pragma once
// Painted damage ground (docs/hazards.md): three sound layers over the wall tones, all driven per frame from the
// in-game screen. Lanes: the sizzle bank on the wall-tone rectangle probes, distance to the first painted cell in
// each compass direction. Bed: the low sizzle pair while the player's own cell is painted. Pointer: while inside,
// every safe island reachable through the paint is pinged in turn at its nearest cell, pitch = north/south, pan =
// east/west. On by default; no persisted settings yet (the config screen is planned).
#include <string>

namespace gd::hazard {
void tick();      // game thread, every frame in the world
void silence();   // stop the loops (screen unfocused / popped)
std::string status();                    // dev: /hazard
bool set_param(const std::string& key, const std::string& value);   // dev knobs; false = unknown key
std::string probe_timing(int iters);     // dev: time one search pass
}
