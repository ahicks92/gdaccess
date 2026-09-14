#pragma once
// Player-facing sound cue settings (the Ctrl+T overlay, screens/cue_settings.cpp): every positioned cue can
// be switched off on its own, and four channel volumes scale what stays on -- wall tones, harmful ground, the sonar's
// enemy ping, the sonar's other pings, and the two positional voices. Persisted through settings (key=value, %LOCALAPPDATA%\gdaccess\settings.txt)
// like the T overlay. The dev knobs (/walltones?vol=, /sonar?vol=, /hazard?vol=) stay separate: these multiply them.
namespace gd::cues {
enum Cue { WallTones = 0, HazardLanes, HazardInside, HazardExit, Enemies, Loot, Entrances, Breakables, Shrines, Interactables, kCues };
enum Channel { Walls = 0, Hazards, EnemyChannel, Other, VoiceMark, VoiceZira, kChannels };   // the two voices: Mark at the enemy, Zira = the player
constexpr int kVolumeStep = 10;   // percent per Left/Right

void init();   // after settings::init
bool enabled(Cue c);
void set_enabled(Cue c, bool on);   // persists
int volume(Channel ch);             // percent 0..100 (default 100)
void set_volume(Channel ch, int percent);   // clamped, persists
float gain(Channel ch);             // volume / 100 as a linear factor (any thread: the voice worker reads it)
}  // namespace gd::cues
