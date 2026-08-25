#pragma once
// The positional voices: rendered speech (OneCore, src/tts_onecore.cpp) played through the mod's own mixer,
// so readouts overlap instead of queueing and each one sits where it happened. Two channels by convention:
// Mark = things happening at an enemy (hits, misses), Zira = the player's own state (health, energy).
// Menus and UI keep the screen-reader channel (src/speech.h). say() never blocks; a worker thread renders
// (cached by text) and plays. Without OneCore the lines fall back to the screen reader, unpanned.
#include <string>
#include <string_view>
#include "ring.h"
namespace gd::voice {
enum class Which { Mark, Zira };
enum class Policy { Overlap, Replace };  // Replace: cut the still-playing line of the same group
constexpr int kGroupEnemy = 1, kGroupSelf = 2, kGroupInfo = 3;   // Info: place announcements (rooms)
constexpr int kGroupSelfEffect = 4;   // status effects the player receives (Zira, panned to the source); kept off kGroupSelf so a health step's Replace does not cut them

struct Say {
  Which voice = Which::Mark;
  std::string text;      // utf-8, composed through strings.h (never concatenated here)
  float pan = 0.0f;      // -1 left .. +1 right
  float gain = 1.0f;     // 0..1, before the voice trim
  Policy policy = Policy::Overlap;
  int group = 0;
  float predelay_ms = 0.0f;  // leading silence, to stagger co-timed identical lines (see audio::play_pcm)
};

bool init();             // starts the worker; false = OneCore never came up (the fallback is active)
void shutdown();         // stops and JOINS the worker; call before audio::shutdown()
void say(Say s);         // any thread
bool ready();            // OneCore bound and the worker alive
void set_enabled(bool on);
bool enabled();
void set_max_concurrent(Which v, int n);  // playing lines per channel before new ones are dropped
void set_gain(float g);                   // trim applied to every voice line (default 1.0)
float gain();
LineRing& history();     // every line handed to say(), tagged with its channel, pan and gain (/voice)
std::string status();    // /voices
}  // namespace gd::voice
