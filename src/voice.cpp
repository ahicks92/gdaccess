#include "voice.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "audio.h"
#include "core/strings.h"
#include "log.h"
#include "speech.h"
#include "tts_onecore.h"

namespace gd::voice {
namespace {
constexpr size_t kQueueCap = 32;
constexpr size_t kCacheEntries = 512;
constexpr size_t kCacheSamples = 48000 * 60;  // ~60 s of audio, ~11 MB
constexpr float kPeak = 0.8f;                   // normalized peak of every rendered line (-2 dBFS)

std::mutex g_qmu;
std::condition_variable g_cv;
std::deque<Say> g_q;
std::thread g_worker;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_ready{false};      // OneCore bound, worker alive
std::atomic<bool> g_started{false};
std::atomic<bool> g_enabled{true};
std::atomic<int> g_max[2] = {4, 2};
std::atomic<float> g_gain{1.0f};
std::atomic<uint64_t> g_said{0}, g_dropped_queue{0}, g_dropped_cap{0}, g_synth_fail{0}, g_cache_hits{0};
std::atomic<bool> g_fallback_logged{false};
LineRing g_history(1000);
std::mutex g_status_mu;
std::string g_init_note;
std::string g_voice_list;  // AllVoices() as text, taken on the worker at init (WinRT is worker-thread only)

// Worker-thread only: the rendered-line cache, LRU by total samples / entry count.
struct Entry { audio::Pcm pcm; uint64_t used; };
std::unordered_map<std::string, Entry> g_cache;
size_t g_cache_samples = 0;
uint64_t g_tick = 0;

const char* tag(Which v) { return v == Which::Mark ? "mark" : "zira"; }
int slot(Which v) { return v == Which::Mark ? 0 : 1; }

void evict_if_needed() {
  while (!g_cache.empty() && (g_cache.size() > kCacheEntries || g_cache_samples > kCacheSamples)) {
    auto victim = g_cache.begin();
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) if (it->second.used < victim->second.used) victim = it;
    g_cache_samples -= victim->second.pcm->size();
    g_cache.erase(victim);
  }
}

audio::Pcm render(const Say& s) {
  std::string key = std::string(1, (char)('0' + slot(s.voice))) + '\0' + s.text;
  auto it = g_cache.find(key);
  if (it != g_cache.end()) { it->second.used = ++g_tick; ++g_cache_hits; return it->second.pcm; }
  std::vector<uint8_t> wav;
  if (!tts::synthesize(slot(s.voice), s.text, wav)) { ++g_synth_fail; return {}; }
  std::vector<float> pcm;
  if (!audio::decode_wav_memory(wav.data(), wav.size(), pcm)) { ++g_synth_fail; log::writef("voice: could not decode the synthesized WAV ({} bytes)", wav.size()); return {}; }
  // OneCore's output level varies by word; normalize every line to the same peak so the trim means one thing.
  float peak = 0.0f;
  for (float v : pcm) peak = std::max(peak, std::fabs(v));
  if (peak > 1e-4f) { float k = kPeak / peak; for (float& v : pcm) v *= k; }
  auto shared = std::make_shared<const std::vector<float>>(std::move(pcm));
  g_cache_samples += shared->size();
  g_cache[key] = {shared, ++g_tick};
  evict_if_needed();
  return shared;
}

void worker() {
  tts::enter_apartment();
  bool ok = tts::init();
  if (ok) {
    tts::select(0, "Mark");
    tts::select(1, "Zira");
    std::string list;
    for (const tts::VoiceInfo& v : tts::voices()) list += std::format("  voice '{}' {} {}\n", v.display_name, v.language, v.id);
    { std::lock_guard lk(g_status_mu); g_voice_list = list; }
    g_ready = true;
  } else {
    std::lock_guard lk(g_status_mu);
    g_init_note = tts::last_error();
  }
  g_started = true;
  while (!g_stop) {
    Say s;
    {
      std::unique_lock lk(g_qmu);
      g_cv.wait(lk, [] { return g_stop || !g_q.empty(); });
      if (g_stop) break;
      s = std::move(g_q.front()); g_q.pop_front();
    }
    if (!g_ready) continue;
    audio::Pcm pcm = render(s);
    if (!pcm) continue;
    if (s.policy == Policy::Overlap && s.group && audio::group_count(s.group) >= g_max[slot(s.voice)].load()) { ++g_dropped_cap; continue; }
    audio::play_pcm(pcm, s.gain * g_gain.load(), s.pan, s.group, s.policy == Policy::Replace, false);
  }
  g_cache.clear();
  g_cache_samples = 0;
  tts::shutdown();
  g_ready = false;
  tts::leave_apartment();
}
}  // namespace

bool init() {
  if (g_worker.joinable()) return g_ready;
  g_stop = false;
  g_worker = std::thread(worker);
  // Wait briefly for the verdict so the load log says which channel combat speech has.
  for (int i = 0; i < 100 && !g_started; ++i) Sleep(20);
  if (!g_ready) log::writef("voice: OneCore not ready ({}); combat lines fall back to the screen reader", g_started ? g_init_note : "still initializing");
  return g_ready;
}

void shutdown() {
  if (!g_worker.joinable()) return;
  g_stop = true;
  g_cv.notify_all();
  g_worker.join();
  std::lock_guard lk(g_qmu);
  g_q.clear();
}

void say(Say s) {
  if (s.text.empty()) return;
  ++g_said;
  g_history.push(std::format("{}{} {} [pan {:+.2f} gain {:.2f}]", tag(s.voice), g_ready ? "" : "(prism)", s.text, s.pan, s.gain));
  log::writef("[voice:{}] {}", tag(s.voice), s.text);
  if (!g_enabled) return;
  if (!g_ready) {
    if (!g_fallback_logged.exchange(true)) log::writef("voice: {}", strings::kVoiceUnavailable);
    speech::speak(s.text, s.policy == Policy::Replace);
    return;
  }
  std::lock_guard lk(g_qmu);
  if (s.policy == Policy::Replace)
    std::erase_if(g_q, [&](const Say& q) { return q.policy == Policy::Replace && q.group == s.group; });
  while (g_q.size() >= kQueueCap) {
    auto victim = g_q.end();
    for (auto it = g_q.begin(); it != g_q.end(); ++it) if (it->policy == Policy::Overlap) { victim = it; break; }
    if (victim == g_q.end()) victim = g_q.begin();
    g_q.erase(victim);
    ++g_dropped_queue;
  }
  g_q.push_back(std::move(s));
  g_cv.notify_one();
}

bool ready() { return g_ready; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }
void set_max_concurrent(Which v, int n) { g_max[slot(v)] = n < 1 ? 1 : n; }
void set_gain(float g) { g_gain = g < 0 ? 0 : g > 2 ? 2 : g; }
float gain() { return g_gain; }
LineRing& history() { return g_history; }

std::string status() {
  size_t depth; { std::lock_guard lk(g_qmu); depth = g_q.size(); }
  std::string note; { std::lock_guard lk(g_status_mu); note = g_init_note; }
  std::string out = std::format("ready={} enabled={} worker={} mark='{}' zira='{}' gain={:.2f} max_mark={} max_zira={}\n", g_ready.load(), g_enabled.load(),
                                g_worker.joinable(), tts::selected(0), tts::selected(1), g_gain.load(), g_max[0].load(), g_max[1].load());
  out += std::format("said={} queue={} dropped_queue={} dropped_cap={} synth_fail={} cache_hits={} last_synth_ms={:.1f} playing_enemy={} playing_self={}\n",
                     g_said.load(), depth, g_dropped_queue.load(), g_dropped_cap.load(), g_synth_fail.load(), g_cache_hits.load(), tts::last_ms(),
                     audio::group_count(kGroupEnemy), audio::group_count(kGroupSelf));
  if (!note.empty()) out += "init_error: " + note + "\n";
  { std::lock_guard lk(g_status_mu); out += g_voice_list; }
  return out;
}
}  // namespace gd::voice
