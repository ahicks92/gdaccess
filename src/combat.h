#pragma once
// Combat readouts (docs/combat-feedback.md): the floating numbers / Miss / Dodge / Block the game draws over
// an enemy you hit are GameEvents of type 0x1b sent through EventManager::Send -- hooked here, parsed, merged
// per place and spoken by Mark where they happened. The player's own health is watched per frame and spoken
// by Zira at 10 % steps; H reads health and energy in full. Game-thread only except status().
#include <string>
namespace gd::combat {
bool install();          // attaches the EventManager::Send hook
void remove();
void tick();             // per frame from the in-game screen (cheap early-out outside the world)
void speak_vitals();     // the H key: "health A of B, energy C of D"
std::string status();    // /combat
void arm_raw_log(int n); // /combat?raw=N: hex-dump the next N raw events into the log (layout confirmation)
void set_coalesce(bool on);
void set_window(double seconds);
void set_cap(int per_flush);
}  // namespace gd::combat
