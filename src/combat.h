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
// The T overlay's switches (persisted by settings): outgoing = your hits, kills and XP (Mark); incoming = your
// health steps and effects applied to you (Zira). Off = the events are still tracked, just not voiced.
// outgoing: 0 off, 1 brief ("hit" / "crit" / "miss" / "blocked"), 2 full (the numbers). incoming hits: "hit" per attack
// that reaches you (the victim-side resolver, so invincibility does not hide it).
int outgoing_mode();
void set_outgoing_mode(int mode);
bool incoming_enabled();
void set_incoming(bool on);
bool incoming_hits_enabled();
void set_incoming_hits(bool on);
std::string status();    // /combat
void arm_raw_log(int n); // /combat?raw=N: hex-dump the next N raw events into the log (layout confirmation)
std::string hit_status();    // /hitsay: the incoming-hit resolver's counters
void set_coalesce(bool on);
void set_window(double seconds);
void set_cap(int per_flush);
}  // namespace gd::combat
