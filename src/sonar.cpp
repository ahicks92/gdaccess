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
enum Kind { kEnemy = 0, kLoot = 1, kTransition = 2 };
constexpr const char* kCue[3] = {"units-enemy.wav", "unknown.wav", "transition.wav"};
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
    items.push_back({it.id, it.dist, (pan + 1.0f) * 0.5f, (int)kind});   // phase 0..1 = left..right
    g_live[it.id] = {pan, gain, ahead, (int)kind};
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
  auto pings = g_field.update(items, now);   // each thing pulses on its own period, phase-staggered
  if (pings.empty() || !audible()) return;
  for (const core::SonarField::Ping& p : pings) {
    auto f = g_live.find(p.id);
    if (f == g_live.end()) continue;
    const Placed& pl = f->second;
    audio::play_sample(audio::module_dir() + "assets\\audio\\interactables\\" + kCue[p.kind],
                       pl.gain * g_vol, pl.pan, world::rear_shelf_db(pl.ahead));
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
std::string status() {
  const core::FieldParams& p = g_field.params();
  std::string s = std::format("enabled={} radius={:.1f} vol={:.2f} rolloff {} (shared with the review pings) "
                              "period {:.2f}s@{:.0f}u .. {:.2f}s@{:.0f}u (log in distance) tracked={} fired={}\n",
                              g_enabled, g_radius, g_vol, world::ping_rolloff(),
                              p.period_near, p.dist_near, p.period_far, p.dist_far, g_field.tracked(), g_fired);
  for (auto [group, name] : {std::pair{world::ScanGroup::Enemies, "enemy"}, std::pair{world::ScanGroup::Loot, "loot"}, std::pair{world::ScanGroup::Transitions, "transition"}})
    for (const world::ScanItem& it : world::scan(group, g_radius)) {
      float pan, gain, ahead; world::ear_frame(it.pos, pan, gain, &ahead);
      s += std::format("  {:<10} {:5.1f} pan {:+.2f} period {:.2f}s ahead {:+.2f} shelf {:+.1f} dB vol {:.2f}  {} '{}'\n",
                       name, it.dist, pan, p.period_for(it.dist), ahead, world::rear_shelf_db(ahead), gain * g_vol, it.cls, it.label);
    }
  return s;
}
}  // namespace gd::sonar
