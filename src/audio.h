#pragma once
// The mod's own sounds (wall-tone loops, cues, test tones): a small mixer on miniaudio's default output.
// Everything is thread-safe and cheap enough to call every frame.
#include <string>
namespace gd::audio {
bool init();
void shutdown();
bool ready();
// A continuous tone keyed by id: set creates or updates it (freq in Hz, volume 0..1, pan -1 left .. +1
// right). Volume changes are smoothed so per-frame updates do not click. volume 0 keeps the tone allocated
// but silent; stop_tone removes it.
void set_tone(int id, float freq, float volume, float pan);
void stop_tone(int id);
// A one-shot beep (volume/pan as above), with a short fade in and out.
void beep(float freq, int ms, float volume, float pan = 0.0f);
// Looping sample channels (the wotr wall tones): a WAV decoded to mono at the mixer rate, looped
// seamlessly at a fixed pan, volume driven per frame (clamped 0..1, applied directly like wotr).
bool load_loop(int id, const std::string& wav_path, float pan);
void set_loop_volume(int id, float volume);
void unload_loop(int id);
// A one-shot sample (WAV decoded once and cached) at volume 0..1 and pan -1..1: the review pings, cues.
void play_sample(const std::string& wav_path, float volume, float pan);
// Directory of this DLL (assets live next to it), with a trailing backslash.
std::string module_dir();
float master_volume();
void set_master_volume(float v);
}  // namespace gd::audio
