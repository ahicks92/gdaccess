#include "combat.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <vector>
#include "app.h"
#include "core/combat_coalesce.h"
#include "core/combat_text.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "core/threshold_watcher.h"
#include "gd_names.h"
#include "hooks.h"
#include "log.h"
#include "msvc_string.h"
#include "speech.h"
#include "voice.h"
#include "world.h"

namespace gd::combat {
namespace {
using namespace gd::names;
using gd::core::MessageBuilder;
constexpr unsigned kCombatTextEvent = 0x1b;

// GameEvent for type 0x1b, from CombatManager::TakeAttack's stack record (static RE 2026-08-22, Game.dll
// +0x10abd4..: type, id, the victim's HeadEffect position, the style variable name, the drawn text, the
// text class 0x46 / 0x85 crit, the scale). Read-only, SEH-guarded, confirmed live via /combat?raw=.
struct CombatTextEvent {
  uint32_t type;            // +0x00 = 0x1b
  uint32_t id;              // +0x04 attacker/entity id (0 on the miss path)
  unsigned char wv[0x18];   // +0x08 WorldVec3 { Region* +0, Vec3 +8 }
  MsvcStringA style;        // +0x20 "missStyle" / "hitStyle" / "petHitStyle" ...
  MsvcStringW text;         // +0x40 as drawn: "Miss" / "123" / "456 (x1.50)"
  uint32_t text_class;      // +0x60 0x46 normal, 0x85 crit
  float scale;              // +0x64
};
static_assert(offsetof(CombatTextEvent, style) == 0x20);
static_assert(offsetof(CombatTextEvent, text) == 0x40);
static_assert(offsetof(CombatTextEvent, text_class) == 0x60);
static_assert(sizeof(CombatTextEvent) == 0x68);

// What the hook keeps: plain data only (SEH rule), handed to tick() on the same thread.
struct RawEvent {
  unsigned id = 0, text_class = 0;
  float scale = 0;
  char style[32] = {};
  char16_t text[64] = {};
  unsigned char wv[0x18] = {};
  bool has_region = false;
  double t = 0;
};

std::vector<gd::hooks::Hook> g_hooks;
std::deque<RawEvent> g_pending;          // game thread only
std::atomic<uint64_t> g_seen{0}, g_parsed{0}, g_spoken{0}, g_bad{0};
std::atomic<int> g_raw_log{0};
gd::core::CombatCoalescer g_coalescer;
gd::core::ThresholdWatcher g_health(0.10);
std::deque<std::string> g_recent;        // last parsed lines, for /combat
std::mutex g_recent_mu;

void note(std::string line) {
  std::lock_guard lk(g_recent_mu);
  g_recent.push_back(std::move(line));
  while (g_recent.size() > 20) g_recent.pop_front();
}

bool bad_ptr(const void* p, size_t n) { return IsBadReadPtr(p, n) != 0; }

// Bounded copies out of the event; the strings may be heap-backed (the crit text is longer than the SSO).
bool read_event_body(const void* ev, RawEvent& out) {
  const CombatTextEvent* e = (const CombatTextEvent*)ev;
  out.id = e->id; out.text_class = e->text_class; out.scale = e->scale;
  memcpy(out.wv, e->wv, sizeof out.wv);
  void* region; memcpy(&region, e->wv, sizeof region);
  out.has_region = region != nullptr;
  size_t n = e->style.size < 31 ? e->style.size : 31;
  const char* sd = e->style.data();
  if (n && bad_ptr(sd, n)) return false;
  memcpy(out.style, sd, n); out.style[n] = 0;
  size_t m = e->text.size < 63 ? e->text.size : 63;
  const char16_t* td = e->text.data();
  if (m && bad_ptr(td, m * 2)) return false;
  memcpy(out.text, td, m * 2); out.text[m] = 0;
  return true;
}
bool read_event(const void* ev, RawEvent& out) {
  __try {
    if (bad_ptr(ev, sizeof(CombatTextEvent))) return false;
    return read_event_body(ev, out);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool copy_raw(const void* ev, unsigned char* b, size_t n) {
  __try { memcpy(b, ev, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void dump_raw(const void* ev) {
  unsigned char b[0x80] = {};
  if (!copy_raw(ev, b, sizeof b)) { log::write("combat: raw event unreadable"); return; }
  std::string hex;
  for (size_t i = 0; i < sizeof b; ++i) { if (i && i % 16 == 0) hex += " | "; hex += std::format("{:02x}", b[i]); }
  log::writef("combat: raw 0x1b at {}: {}", ev, hex);
}

typedef void (*EventManagerSend_t)(void*, const void*, unsigned);
static EventManagerSend_t EventManagerSend_hook_orig;
static void EventManagerSend_hook(void* self, const void* ev, unsigned type) {
  EventManagerSend_hook_orig(self, ev, type);  // the game's behaviour first, always
  if (type != kCombatTextEvent || !ev) return;
  ++g_seen;
  if (g_raw_log > 0) { --g_raw_log; dump_raw(ev); }
  RawEvent r;
  if (!read_event(ev, r)) { ++g_bad; return; }
  r.t = app::now();
  if (g_pending.size() < 256) g_pending.push_back(r);
}

std::string hit_line(const gd::core::CombatCoalescer::Out& o) {
  MessageBuilder m;
  if (o.is_number) strings::push_combat_hit(m, std::format("{:.0f}", o.amount), o.crit);
  else strings::push_combat_word(m, o.word);
  return m.build();
}
}  // namespace

bool install() {
  g_hooks = {GD_HOOK(EventManager_Send, EventManagerSend_hook)};
  return gd::hooks::attach_hooks(g_hooks) == 0;
}
void remove() { gd::hooks::detach_hooks(g_hooks); g_pending.clear(); }

void tick() {
  if (!world::in_world()) { g_health.reset(); g_pending.clear(); g_coalescer.clear(); return; }
  double now = app::now();
  while (!g_pending.empty()) {
    RawEvent r = std::move(g_pending.front()); g_pending.pop_front();
    ++g_parsed;
    std::string drawn = log::utf8(r.text);
    gd::core::CombatText ct = gd::core::parse_combat_text(drawn, r.text_class == 0x85);
    world::Vec3 p{}; float pan = 0.0f, gain = 1.0f;
    bool placed = r.has_region && world::world_point(r.wv, p);
    if (placed) {
      world::ear_frame(p, pan, gain);
      world::Vec3 me; float dist = 0.0f;
      if (world::player_position(me)) { float dx = p.x - me.x, dz = p.z - me.z; dist = std::sqrt(dx * dx + dz * dz); }
      gain = world::voice_gain(dist);
    }
    note(std::format("{} '{}' class={:#x} style={} id={} pos=({:.1f},{:.1f}) pan={:+.2f} gain={:.2f}", ct.is_number ? (ct.crit ? "crit" : "hit") : "word",
                     drawn, r.text_class, r.style, r.id, p.x, p.z, pan, gain));
    g_coalescer.push({ct.is_number, ct.amount, ct.crit, ct.word, p.x, p.z, pan, gain, r.t});
  }
  for (const auto& o : g_coalescer.flush(now)) {
    ++g_spoken;
    voice::say({voice::Which::Mark, hit_line(o), o.pan, o.gain, voice::Policy::Overlap, voice::kGroupEnemy});
  }
  float mx = world::life_max();
  if (mx > 0) {
    int pct = 0;
    if (g_health.update(world::life() / mx, pct)) {
      MessageBuilder m;
      strings::push_health_percent(m, pct);
      voice::say({voice::Which::Zira, m.build(), 0.0f, 1.0f, voice::Policy::Replace, voice::kGroupSelf});
    }
  }
}

// The H key is a screen-reader readout like every other key (the positional voices are only for things that
// happen in the world, not for what the player asked for).
void speak_vitals() {
  if (!world::in_world()) { speech::speak(strings::kNotInWorld, true); return; }
  MessageBuilder m;
  strings::push_vitals(m, world::life(), world::life_max(), world::energy(), world::energy_max());
  speech::speak(m.build(), true);
}

std::string status() {
  std::string out = std::format("events_seen={} parsed={} spoken={} unreadable={} pending={} coalesce={} window={:.3f} merged={} dropped={} health_bucket={}\n",
                                g_seen.load(), g_parsed.load(), g_spoken.load(), g_bad.load(), g_pending.size(), g_coalescer.enabled(), g_coalescer.window(),
                                g_coalescer.merged(), g_coalescer.dropped(), g_health.bucket());
  std::lock_guard lk(g_recent_mu);
  for (const std::string& l : g_recent) out += "  " + l + "\n";
  return out;
}
void arm_raw_log(int n) { g_raw_log = n; }
void set_coalesce(bool on) { g_coalescer.set_enabled(on); }
void set_window(double seconds) { g_coalescer.set_window(seconds); }
void set_cap(int per_flush) { g_coalescer.set_max_per_flush(per_flush); }
}  // namespace gd::combat
