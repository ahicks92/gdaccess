#include "sonar.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include "app.h"
#include "audio.h"
#include "core/sonar_sweep.h"
#include "log.h"
#include "world.h"

namespace gd::sonar {
namespace {
// wotr's defaults: 40 ft of reach, a 10 ft reference distance, 40 % volume. Grim Dawn's unit is about a
// metre and its outdoors are wide, so the reach is larger here; all live-tunable (/sonar).
float g_radius = 25.0f;   // world units
float g_vol = 0.4f;       // the cue's own level (the mixer's master applies on top)
float g_ref = 4.0f;       // units: volume = ref / (ref + dist), floor 0.08 (wotr VolumeFor)
bool g_enabled = true;
bool g_force = false;      // dev: ignore the foreground gate (the dev loop never focuses the game)
core::SonarSweep g_sweep;
enum Kind { kEnemy = 0, kLoot = 1, kTransition = 2 };
constexpr const char* kCue[3] = {"units-enemy.wav", "unknown.wav", "transition.wav"};
struct Cached { unsigned id; world::Vec3 pos; };
std::vector<Cached> g_positions;   // the sweep's entries' positions at sweep start (id -> pos)
int g_fired = 0;

float volume_for(float dist) {
  float v = g_ref / (g_ref + dist);
  return std::clamp(v, 0.08f, 1.0f) * g_vol;
}
bool audible() {
  if (g_force) return true;
  HWND fg = GetForegroundWindow();
  return fg && fg == FindWindowA("Grim Dawn", nullptr);
}
void collect(world::ScanGroup group, Kind kind, std::vector<core::SweepEntry>& entries) {
  for (const world::ScanItem& it : world::scan(group, g_radius)) {
    float pan, gain; world::ear_frame(it.pos, pan, gain);
    entries.push_back({it.id, pan * it.dist, (int)kind});
    g_positions.push_back({it.id, it.pos});
  }
}
}  // namespace

void tick() {
  if (!g_enabled || !world::in_world()) { g_sweep.reset(); return; }
  double now = app::now();
  if (g_sweep.wants_entries(now)) {
    std::vector<core::SweepEntry> entries;
    g_positions.clear();
    collect(world::ScanGroup::Enemies, kEnemy, entries);
    collect(world::ScanGroup::Loot, kLoot, entries);
    collect(world::ScanGroup::Transitions, kTransition, entries);
    g_sweep.begin(std::move(entries), now);
  }
  auto e = g_sweep.next(now);
  if (!e) return;
  // The position from the sweep snapshot; the ear frame is re-evaluated now, so the player having moved is accounted for.
  world::Vec3 pos{};
  bool have = false;
  for (const Cached& c : g_positions) if (c.id == e->id) { pos = c.pos; have = true; break; }
  if (!have) return;
  world::Vec3 me;
  if (!world::player_position(me)) return;
  float dist = std::sqrt((pos.x - me.x) * (pos.x - me.x) + (pos.z - me.z) * (pos.z - me.z));
  float pan, gain, ahead; world::ear_frame(pos, pan, gain, &ahead);
  if (!audible()) return;
  audio::play_sample(audio::module_dir() + "assets\\audio\\interactables\\" + kCue[e->kind], volume_for(dist), pan, world::rear_shelf_db(ahead));
  ++g_fired;
}

void reset() { g_sweep.reset(); g_positions.clear(); }
void set_enabled(bool on) { g_enabled = on; if (!on) reset(); }
bool enabled() { return g_enabled; }
void set_knob(const std::string& name, float v) {
  core::SweepParams& p = g_sweep.params();
  if (name == "radius" && v > 1) g_radius = v;
  else if (name == "vol") g_vol = std::clamp(v, 0.0f, 1.0f);
  else if (name == "ref" && v > 0.1f) g_ref = v;
  else if (name == "gap_min" && v > 0) p.gap_min_s = v;
  else if (name == "gap_max" && v > 0) p.gap_max_s = v;
  else if (name == "rest" && v >= 0) p.rest_s = v;
  else if (name == "force") g_force = v != 0;
}
std::string status() {
  const core::SweepParams& p = g_sweep.params();
  std::string s = std::format("enabled={} radius={:.1f} vol={:.2f} ref={:.1f} gap_min={:.2f} gap_max={:.2f} rest={:.2f} fired={} in_sweep={}\n",
                              g_enabled, g_radius, g_vol, g_ref, p.gap_min_s, p.gap_max_s, p.rest_s, g_fired, g_sweep.remaining());
  for (auto [group, name] : {std::pair{world::ScanGroup::Enemies, "enemy"}, std::pair{world::ScanGroup::Loot, "loot"}, std::pair{world::ScanGroup::Transitions, "transition"}})
    for (const world::ScanItem& it : world::scan(group, g_radius)) {
      float pan, gain, ahead; world::ear_frame(it.pos, pan, gain, &ahead);
      s += std::format("  {:<10} {:5.1f} pan {:+.2f} ahead {:+.2f} shelf {:+.1f} dB vol {:.2f}  {} '{}'\n", name, it.dist, pan, ahead, world::rear_shelf_db(ahead), volume_for(it.dist), it.cls, it.label);
    }
  return s;
}
}  // namespace gd::sonar
