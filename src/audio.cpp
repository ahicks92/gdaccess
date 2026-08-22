#include "audio.h"
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_DEVICE_IO_CUSTOM_BACKEND
#include <miniaudio.h>
#include <windows.h>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>
#include "log.h"

namespace gd::audio {
namespace {
constexpr int kRate = 48000;
constexpr float kPi = 3.14159265358979f;

struct Tone {
  int id;                 // >= 0: keyed continuous tone; -1: one-shot beep
  float freq;
  float phase = 0;
  float vol = 0;          // current (smoothed)
  float target = 0;       // requested
  float pan = 0;
  int remaining = -1;     // beep: samples left (-1 = continuous)
  bool dead = false;
};
// A looping mono sample at a fixed pan; volume applied directly (the wotr WallTones channel).
struct Loop {
  int id;
  std::vector<float> samples;
  size_t pos = 0;
  float volume = 0;
  float gl = 0.70710677f, gr = 0.70710677f;
  ULONGLONG refreshed = 0;  // GetTickCount64 at the last set_loop_volume: a loop nobody drives goes silent
};
constexpr ULONGLONG kLoopStaleMs = 250;  // the game stops ticking us while paused; the loops must not sing on

// A one-shot sample voice: plays a cached mono buffer once at fixed gains, then is removed.
struct Shot {
  const std::vector<float>* samples;
  size_t pos = 0;
  float gl, gr;
};

std::mutex g_mu;
std::vector<Tone> g_tones;
std::vector<Loop> g_loops;
std::vector<Shot> g_shots;
std::map<std::string, std::vector<float>> g_sample_cache;  // decoded mono at the mixer rate; never freed
ma_device g_device;
bool g_ready = false;
float g_master = 0.6f;

void pan_gains(float pan, float& l, float& r) {
  float a = (pan + 1.0f) * (kPi / 4.0f);  // equal power
  l = std::cos(a); r = std::sin(a);
}

void data_callback(ma_device*, void* out, const void*, ma_uint32 frames) {
  float* o = (float*)out;
  for (ma_uint32 i = 0; i < frames * 2; ++i) o[i] = 0;
  std::lock_guard lk(g_mu);
  const float master = g_master;
  for (Tone& t : g_tones) {
    if (t.dead) continue;
    float l, r; pan_gains(t.pan, l, r);
    float step = 2.0f * kPi * t.freq / kRate;
    for (ma_uint32 i = 0; i < frames; ++i) {
      float target = t.target;
      if (t.remaining >= 0) {
        if (t.remaining == 0) { target = 0; if (t.vol < 0.001f) { t.dead = true; break; } }
        else --t.remaining;
        if (t.remaining < 240) target = 0;  // 5 ms fade out at the end of a beep
      }
      t.vol += (target - t.vol) * 0.002f;   // ~10 ms smoothing
      float s = std::sin(t.phase) * t.vol * master;
      t.phase += step; if (t.phase > 2.0f * kPi) t.phase -= 2.0f * kPi;
      o[i * 2] += s * l; o[i * 2 + 1] += s * r;
    }
  }
  const ULONGLONG now = GetTickCount64();
  for (Loop& lp : g_loops) {
    float vol = now - lp.refreshed > kLoopStaleMs ? 0.0f : lp.volume;  // watchdog: silent unless driven
    if (lp.samples.empty() || vol <= 0.0f) { if (!lp.samples.empty()) lp.pos = (lp.pos + frames) % lp.samples.size(); continue; }
    for (ma_uint32 i = 0; i < frames; ++i) {
      float s = lp.samples[lp.pos] * vol;  // loops carry their own gain (set by the driver), not the master
      if (++lp.pos >= lp.samples.size()) lp.pos = 0;  // seamless wrap
      o[i * 2] += s * lp.gl; o[i * 2 + 1] += s * lp.gr;
    }
  }
  for (Shot& sh : g_shots) {
    const std::vector<float>& s = *sh.samples;
    for (ma_uint32 i = 0; i < frames && sh.pos < s.size(); ++i, ++sh.pos) {
      float v = s[sh.pos] * master;
      o[i * 2] += v * sh.gl; o[i * 2 + 1] += v * sh.gr;
    }
  }
  std::erase_if(g_shots, [](const Shot& sh) { return sh.pos >= sh.samples->size(); });
  for (ma_uint32 i = 0; i < frames * 2; ++i) o[i] = o[i] > 1.0f ? 1.0f : o[i] < -1.0f ? -1.0f : o[i];
  std::erase_if(g_tones, [](const Tone& t) { return t.dead; });
}
}  // namespace

bool init() {
  if (g_ready) return true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = kRate;
  cfg.dataCallback = data_callback;
  cfg.periodSizeInMilliseconds = 10;
  if (ma_device_init(nullptr, &cfg, &g_device) != MA_SUCCESS) { log::write("audio: device init failed"); return false; }
  if (ma_device_start(&g_device) != MA_SUCCESS) { log::write("audio: device start failed"); ma_device_uninit(&g_device); return false; }
  g_ready = true;
  log::writef("audio: output ready ({} Hz, {} ms periods)", kRate, cfg.periodSizeInMilliseconds);
  return true;
}
void shutdown() {
  if (!g_ready) return;
  g_ready = false;
  ma_device_uninit(&g_device);
  std::lock_guard lk(g_mu);
  g_tones.clear();
  g_loops.clear();
}
bool ready() { return g_ready; }

void set_tone(int id, float freq, float volume, float pan) {
  std::lock_guard lk(g_mu);
  for (Tone& t : g_tones)
    if (t.id == id && !t.dead) { t.freq = freq; t.target = volume; t.pan = pan; return; }
  Tone t{id, freq};
  t.target = volume; t.pan = pan;
  g_tones.push_back(t);
}
void stop_tone(int id) {
  std::lock_guard lk(g_mu);
  for (Tone& t : g_tones) if (t.id == id) { t.target = 0; t.remaining = 0; }
}
void beep(float freq, int ms, float volume, float pan) {
  std::lock_guard lk(g_mu);
  Tone t{-1, freq};
  t.target = volume; t.pan = pan; t.remaining = ms * kRate / 1000;
  g_tones.push_back(t);
}

bool load_loop(int id, const std::string& wav_path, float pan) {
  // Decode to mono float at the mixer rate (miniaudio resamples; the wotr files are 44.1 kHz).
  ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1, kRate);
  ma_decoder dec;
  if (ma_decoder_init_file(wav_path.c_str(), &dc, &dec) != MA_SUCCESS) { log::writef("audio: cannot open loop '{}'", wav_path); return false; }
  std::vector<float> samples;
  float buf[4096];
  for (;;) {
    ma_uint64 got = 0;
    ma_result r = ma_decoder_read_pcm_frames(&dec, buf, 4096, &got);
    samples.insert(samples.end(), buf, buf + got);
    if (r != MA_SUCCESS || got == 0) break;
  }
  ma_decoder_uninit(&dec);
  if (samples.empty()) { log::writef("audio: loop '{}' decoded to nothing", wav_path); return false; }
  Loop lp{id};
  lp.samples = std::move(samples);
  pan_gains(pan, lp.gl, lp.gr);
  std::lock_guard lk(g_mu);
  std::erase_if(g_loops, [id](const Loop& l) { return l.id == id; });
  g_loops.push_back(std::move(lp));
  log::writef("audio: loop {} = '{}' ({} frames, pan {:.1f})", id, wav_path, g_loops.back().samples.size(), pan);
  return true;
}
void set_loop_volume(int id, float volume) {
  std::lock_guard lk(g_mu);
  for (Loop& l : g_loops) if (l.id == id) { l.volume = volume < 0 ? 0 : volume > 1 ? 1 : volume; l.refreshed = GetTickCount64(); }
}
void unload_loop(int id) {
  std::lock_guard lk(g_mu);
  std::erase_if(g_loops, [id](const Loop& l) { return l.id == id; });
}

void play_sample(const std::string& wav_path, float volume, float pan) {
  const std::vector<float>* buf = nullptr;
  {
    std::lock_guard lk(g_mu);
    auto it = g_sample_cache.find(wav_path);
    if (it != g_sample_cache.end()) buf = &it->second;
  }
  if (!buf) {
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 1, kRate);
    ma_decoder dec;
    if (ma_decoder_init_file(wav_path.c_str(), &dc, &dec) != MA_SUCCESS) { log::writef("audio: cannot open sample '{}'", wav_path); return; }
    std::vector<float> samples;
    float tmp[4096];
    for (;;) {
      ma_uint64 got = 0;
      ma_result r = ma_decoder_read_pcm_frames(&dec, tmp, 4096, &got);
      samples.insert(samples.end(), tmp, tmp + got);
      if (r != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);
    std::lock_guard lk(g_mu);
    buf = &(g_sample_cache[wav_path] = std::move(samples));  // node-stable: the map never erases
  }
  float l, r; pan_gains(pan, l, r);
  volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
  std::lock_guard lk(g_mu);
  g_shots.push_back({buf, 0, volume * l, volume * r});
}

std::string module_dir() {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&module_dir, &m);
  char path[MAX_PATH] = {};
  GetModuleFileNameA(m, path, MAX_PATH);
  std::string s = path;
  size_t slash = s.find_last_of('\\');
  return slash == std::string::npos ? std::string() : s.substr(0, slash + 1);
}
float master_volume() { return g_master; }
void set_master_volume(float v) { g_master = v < 0 ? 0 : v > 1 ? 1 : v; }
}  // namespace gd::audio
