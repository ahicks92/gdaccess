#pragma once
// The mod's own sounds (wall-tone loops, cues, test tones, rendered speech): a small mixer on miniaudio's
// default output. Everything is thread-safe and cheap enough to call every frame.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
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
void set_loop_gain(int id, float gain);   // static trim multiplied with the volume (not clamped; loudness matching)
void unload_loop(int id);
// A one-shot sample (WAV decoded once and cached) at volume 0..1 and pan -1..1: the review pings, cues.
void play_sample(const std::string& wav_path, float volume, float pan);
// Raw PCM one-shots (rendered speech): mono f32 at the mixer rate, shared so a cache may drop its copy while
// the shot still plays. group > 0 tags the shot; replace_group fades out (5 ms) the group's playing shots
// first -- "the new health line replaces the old one". Returns the shot id (0 = not played).
using Pcm = std::shared_ptr<const std::vector<float>>;
uint32_t play_pcm(Pcm samples, float volume, float pan, int group = 0, bool replace_group = false, bool apply_master = true);
void stop_group(int group);        // fade out every playing shot of the group
int group_count(int group);        // shots of the group still playing
// Decode a WAV held in memory to mono f32 at the mixer rate (parse + downmix + resample in one step).
bool decode_wav_memory(const void* data, size_t bytes, std::vector<float>& out);
int sample_rate();
// Directory of this DLL (assets live next to it), with a trailing backslash.
std::string module_dir();
float master_volume();
void set_master_volume(float v);
}  // namespace gd::audio
