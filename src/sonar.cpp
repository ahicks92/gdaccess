#include "sonar.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include "app.h"
#include "audio.h"
#include "core/sonar_field.h"
#include "log.h"
#include "world.h"

namespace gd::sonar {
namespace {
float g_radius = 25.0f;   // world units
float g_vol = 1.0f;       // the channel volume on top of world::ear_frame's curve (the mixer's master applies after)
bool g_enabled = true;
bool g_force = false;      // dev: ignore the foreground gate (the dev loop never focuses the game)
core::SonarField g_field;
// One cue per kind (assets/audio/interactables). Shrines: a ruined / desecrated one has its own cue, a restored one
// the loot "search point" cue. kTrimDb: per-file level trims so the cues sit at one perceived loudness without
// editing the files (tools/loudness.py, K-weighted; see docs/sonar-loudness.md) -- live: /sonar?trim=<kind>,<dB>.
enum Kind { kEnemy = 0, kLoot = 1, kTransition = 2, kDestructible = 3, kShrineRuined = 4, kShrineRestored = 5, kInteractable = 6, kKinds = 7 };
constexpr const char* kCue[kKinds] = {"units-enemy.wav", "unknown.wav", "transition.wav", "destructible.wav", "shrine-ruined.wav", "unknown.wav", "interactable.wav"};
constexpr const char* kKindName[kKinds] = {"enemy", "loot", "transition", "destructible", "shrine-ruined", "shrine-restored", "interactable"};
// Measured 2026-08-26 (tools/loudness.py, K-weighted, reference = units-enemy at -13.9 LKFS): unknown -18.2,
// door05 -22.5 (peak -1.6 dBFS, so only +1.5 fits without clipping; it stays ~7 dB under by the meter),
// push33 -12.6, push17 -12.7 (peak +0.4 raw), interactable (se_old_pack00 buble05) -18.6 (peak -5.5, +4.7 leaves
// 0.8 dB headroom). Order = Kind.
constexpr float kDefaultTrimDb[kKinds] = {0.0f, 4.3f, 1.5f, -1.3f, -1.2f, 4.3f, 4.7f};
float g_trim_db[kKinds] = {0.0f, 4.3f, 1.5f, -1.3f, -1.2f, 4.3f, 4.7f};
float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }
long long g_fired = 0;

// This frame's audible frame for each nearby id, so a ping is placed with the current pan/gain when it fires.
struct Placed { float pan, gain, ahead; int kind; };
std::unordered_map<unsigned, Placed> g_live;

bool audible() {
  if (g_force) return true;
  HWND fg = GetForegroundWindow();
  return fg && fg == FindWindowA("Grim Dawn", nullptr);
}
void collect(world::ScanGroup group, Kind kind, std::vector<core::SonarField::Item>& items) {
  for (const world::ScanItem& it : world::scan(group, g_radius)) {
    float pan, gain, ahead; world::ear_frame(it.pos, pan, gain, &ahead);
    Kind k = kind;
    if (group == world::ScanGroup::Shrines) k = world::shrine_restored(it.id) ? kShrineRestored : kShrineRuined;
    items.push_back({it.id, it.dist, (pan + 1.0f) * 0.5f, (int)k});   // phase 0..1 = left..right
    g_live[it.id] = {pan, gain, ahead, (int)k};
  }
}
}  // namespace

void tick() {
  if (!g_enabled || !world::in_world()) { g_field.reset(); g_live.clear(); return; }
  double now = app::now();
  std::vector<core::SonarField::Item> items;
  g_live.clear();
  collect(world::ScanGroup::Enemies, kEnemy, items);
  collect(world::ScanGroup::Loot, kLoot, items);
  collect(world::ScanGroup::Transitions, kTransition, items);
  collect(world::ScanGroup::Destructibles, kDestructible, items);
  collect(world::ScanGroup::Shrines, kShrineRuined, items);
  collect(world::ScanGroup::Interactables, kInteractable, items);
  auto pings = g_field.update(items, now);   // each thing pulses on its own period, phase-staggered
  if (pings.empty() || !audible()) return;
  for (const core::SonarField::Ping& p : pings) {
    auto f = g_live.find(p.id);
    if (f == g_live.end()) continue;
    const Placed& pl = f->second;
    audio::play_sample(audio::module_dir() + "assets\\audio\\interactables\\" + kCue[p.kind],
                       pl.gain * g_vol * db_to_gain(g_trim_db[pl.kind]), pl.pan, world::rear_shelf_db(pl.ahead));
    ++g_fired;
  }
}

void reset() { g_field.reset(); g_live.clear(); }
void set_enabled(bool on) { g_enabled = on; if (!on) reset(); }
bool enabled() { return g_enabled; }
void set_knob(const std::string& name, float v) {
  core::FieldParams& p = g_field.params();
  if (name == "radius" && v > 1) g_radius = v;
  else if (name == "vol") g_vol = std::clamp(v, 0.0f, 1.0f);
  else if (name == "ref") world::set_ping_rolloff(v, -1.0f);
  else if (name == "floor") world::set_ping_rolloff(-1.0f, v);
  else if (name == "pnear" && v > 0) p.period_near = v;   // period at dist_near (fast)
  else if (name == "pfar" && v > 0) p.period_far = v;     // period at dist_far (slow)
  else if (name == "dnear" && v > 0) p.dist_near = v;
  else if (name == "dfar" && v > 0) p.dist_far = v;
  else if (name == "force") g_force = v != 0;
}
bool set_trim(const std::string& kind, float db) {   // /sonar?trim=<kind>,<dB>; kind "all" restores the defaults (all,1 = flat)
  if (kind == "all") { for (int i = 0; i < kKinds; ++i) g_trim_db[i] = db == 0 ? kDefaultTrimDb[i] : 0.0f; return true; }   // all,0 = defaults; all,1 = flat
  for (int i = 0; i < kKinds; ++i) if (kind == kKindName[i]) { g_trim_db[i] = db; return true; }
  return false;
}
std::string status() {
  const core::FieldParams& p = g_field.params();
  std::string s = std::format("enabled={} radius={:.1f} vol={:.2f} rolloff {} (shared with the review pings) "
                              "period {:.2f}s@{:.0f}u .. {:.2f}s@{:.0f}u (log in distance) tracked={} fired={}\n",
                              g_enabled, g_radius, g_vol, world::ping_rolloff(),
                              p.period_near, p.dist_near, p.period_far, p.dist_far, g_field.tracked(), g_fired);
  s += "trims dB:"; for (int i = 0; i < kKinds; ++i) s += std::format(" {}={:+.1f}", kKindName[i], g_trim_db[i]); s += "\n";
  for (auto [group, kind] : {std::pair{world::ScanGroup::Enemies, kEnemy}, std::pair{world::ScanGroup::Loot, kLoot}, std::pair{world::ScanGroup::Transitions, kTransition},
                             std::pair{world::ScanGroup::Destructibles, kDestructible}, std::pair{world::ScanGroup::Shrines, kShrineRuined},
                             std::pair{world::ScanGroup::Interactables, kInteractable}})
    for (const world::ScanItem& it : world::scan(group, g_radius)) {
      float pan, gain, ahead; world::ear_frame(it.pos, pan, gain, &ahead);
      const char* name = kKindName[group == world::ScanGroup::Shrines ? (world::shrine_restored(it.id) ? kShrineRestored : kShrineRuined) : kind];
      s += std::format("  {:<10} {:5.1f} pan {:+.2f} period {:.2f}s ahead {:+.2f} shelf {:+.1f} dB vol {:.2f}  {} '{}'\n",
                       name, it.dist, pan, p.period_for(it.dist), ahead, world::rear_shelf_db(ahead), gain * g_vol, it.cls, it.label);
    }
  return s;
}
}  // namespace gd::sonar
