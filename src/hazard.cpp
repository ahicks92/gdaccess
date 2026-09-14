#include "hazard.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <map>
#include <vector>
#include "app.h"
#include "audio.h"
#include "log.h"
#include "world.h"

// Painted damage sectors are the engine's own hazard floor: a per-cell layer in each chunk that
// TickManager::Tick reads once a second and turns into rate * max life through CombatManager::ApplyDamage,
// resistances never consulted (docs/hazards.md). world::hazard_at is that lookup for any point; everything here
// is what to do with it. Sounds (assets/audio/hazards, tools/gen_hazard_cues.py, chosen by ear 2026-09-13):
//   sizzle/<dir>.wav   the lane bank, the wall tones' per-direction pitch bands with a rough impulse texture
//   inside_low/*.wav   the "you are standing in it" bed, a 260 Hz sizzle pair played wide
//   the pointer        a synthesized triangle pulse (audio::pulse), one per reachable safe island in turn
namespace gd::hazard {
namespace {
constexpr float kDirs[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};   // world north, east, south, west
constexpr const char* kLaneFile[4] = {"north.wav", "east.wav", "south.wav", "west.wav"};
constexpr float kLanePan[4] = {0.0f, 1.0f, 0.0f, -1.0f};
constexpr int kLaneLoopId = 110;   // 110..113; the wall tones are 100..103
constexpr int kBedLoopId = 114;    // 114 left, 115 right
constexpr float kStep = 0.5f;
constexpr float kLaneSpacing = 0.5f;

// ---- knobs (dev route /hazard) ----
bool g_enabled = true;
float g_range = 10.0f;      // lane range, units
int g_lanes = 3;            // side lanes each way, like the wall tones (half-width 1.5 u)
float g_gain = 1.0f;        // lane loop volume
float g_bed_gain = 1.0f;    // bed loop volume
float g_spread = 0.5f;      // bed pan: left at -spread, right at +spread
float g_period = 0.5f;      // seconds between island pulses
float g_gap = 0.8f;         // extra pause after the last island of a round
int g_cap = 4;              // islands per round, nearest first
int g_radius = 15;          // search radius, cells (1 unit)
float g_pulse_vol = 0.35f;
int g_pulse_ms = 90;
float g_pitch_lo = 220.0f;  // due south
float g_pitch_hi = 880.0f;  // due north
float g_hyst = 2.0f;        // an island overtakes the one ahead only when nearer by this many units
float g_search_s = 0.25f;   // seconds between island searches

// ---- state ----
bool g_loaded = false;
float g_loaded_spread = -1.0f;
float g_dist[4] = {0, 0, 0, 0};
bool g_inside = false;
float g_rate = 0;
int g_type = 0;
double g_last_search = 0;
double g_next_pulse = 0;
int g_cycle = 0;

struct Cell { int x, z; bool operator<(const Cell& o) const { return x != o.x ? x < o.x : z < o.z; } bool operator==(const Cell& o) const { return x == o.x && z == o.z; } };
struct Island {
  std::vector<Cell> cells;   // clean cells (the exit ground), for identity across searches
  Cell entry{0, 0};          // the clean cell with the shortest painted walk from the player
  int walk = 0;              // that walk, in cells
  double first_seen = 0;
};
std::vector<Island> g_islands;

void ensure_loaded() {
  if (g_loaded && g_loaded_spread == g_spread) return;
  std::string dir = audio::module_dir() + "assets\\audio\\hazards\\";
  if (!g_loaded) for (int i = 0; i < 4; ++i) audio::load_loop(kLaneLoopId + i, dir + "sizzle\\" + kLaneFile[i], kLanePan[i]);
  if (g_loaded) { audio::unload_loop(kBedLoopId); audio::unload_loop(kBedLoopId + 1); }
  audio::load_loop(kBedLoopId, dir + "inside_low\\left.wav", -g_spread);
  audio::load_loop(kBedLoopId + 1, dir + "inside_low\\right.wav", g_spread);
  g_loaded = true; g_loaded_spread = g_spread;
}

// The lane rectangle in direction i: each lane walks from the player to its wall (free_distance_ray) in kStep and
// stops at the first painted sample; the direction's distance is the NEAREST lane (any paint you could step onto
// counts, unlike a wall, which needs the whole rectangle). range = nothing within range.
float lane_distance(int i, const world::Vec3& p) {
  float best = g_range;
  for (int k = -g_lanes; k <= g_lanes; ++k) {
    float lateral = k * kLaneSpacing;
    float wall = world::free_distance_ray(kDirs[i][0], kDirs[i][1], lateral, g_range, nullptr);
    if (wall <= 0.0f) continue;   // the lane starts inside a wall
    float sx = p.x - kDirs[i][1] * lateral, sz = p.z + kDirs[i][0] * lateral;   // lateral positive = left of dir (world.cpp)
    for (float s = kStep; s <= wall && s < best; s += kStep) {
      world::Vec3 q{sx + kDirs[i][0] * s, p.y, sz + kDirs[i][1] * s};
      if (world::hazard_at(q, nullptr, nullptr)) { best = s; break; }
    }
    if (best <= kStep) break;
  }
  return best;
}

// ---- the island search ----
// From the player's (painted) cell, breadth-first through painted, on-mesh cells out to g_radius. A clean on-mesh
// cell reached that way is exit ground; exits are then flooded among clean cells (never back through paint) and
// each connected piece is an island with its entry = the exit cell of the shortest painted walk. Off-mesh cells are
// walls. Every cell is classified at most once per search: at most (2r+1)^2 lookups, each a few pointer hops
// (sector) plus one closest-point query (mesh).
enum class Kind : unsigned char { Unknown, Wall, Paint, Clean };
struct Search {
  world::Vec3 origin; int ox, oz;
  std::map<Cell, Kind> kind;
  Kind classify(const Cell& c) {
    auto it = kind.find(c);
    if (it != kind.end()) return it->second;
    world::Vec3 q{(float)c.x + 0.5f, origin.y, (float)c.z + 0.5f};
    Kind k = !world::mesh_contains(q) ? Kind::Wall : world::hazard_at(q, nullptr, nullptr) ? Kind::Paint : Kind::Clean;
    kind[c] = k; return k;
  }
  bool inside(const Cell& c) const { return std::abs(c.x - ox) <= g_radius && std::abs(c.z - oz) <= g_radius; }
};
std::vector<Island> find_islands(const world::Vec3& p, double now) {
  Search s{p, (int)std::floor(p.x), (int)std::floor(p.z)};
  Cell start{s.ox, s.oz};
  s.kind[start] = Kind::Paint;   // we are standing in it (the caller checked)
  std::map<Cell, int> walk;      // painted cells: BFS distance; exit cells: the walk that reached them
  std::vector<Cell> exits;
  std::deque<Cell> q; q.push_back(start); walk[start] = 0;
  const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
  while (!q.empty()) {
    Cell c = q.front(); q.pop_front();
    int d = walk[c];
    for (int n = 0; n < 4; ++n) {
      Cell m{c.x + dx[n], c.z + dz[n]};
      if (!s.inside(m) || walk.count(m)) continue;
      Kind k = s.classify(m);
      if (k == Kind::Wall) continue;
      walk[m] = d + 1;
      if (k == Kind::Paint) q.push_back(m); else exits.push_back(m);
    }
  }
  // flood the exits among clean cells; label islands
  std::map<Cell, int> label; std::vector<Island> out;
  for (const Cell& e : exits) {
    if (label.count(e)) { Island& is = out[label[e]]; if (walk[e] < is.walk) { is.walk = walk[e]; is.entry = e; } continue; }
    int id = (int)out.size(); out.push_back(Island{{}, e, walk[e], now});
    std::deque<Cell> f; f.push_back(e); label[e] = id;
    while (!f.empty()) {
      Cell c = f.front(); f.pop_front(); out[id].cells.push_back(c);
      auto w = walk.find(c);
      if (w != walk.end() && w->second < out[id].walk) { out[id].walk = w->second; out[id].entry = c; }
      for (int n = 0; n < 4; ++n) {
        Cell m{c.x + dx[n], c.z + dz[n]};
        if (!s.inside(m) || label.count(m)) continue;
        if (s.classify(m) != Kind::Clean) continue;
        label[m] = id; f.push_back(m);
      }
    }
  }
  return out;
}
// Keep the previous round's order where the islands persist (identity = shared cells); newcomers go last; an island
// moves ahead of its predecessor only when nearer by more than g_hyst. Then cap.
void merge_islands(std::vector<Island> fresh, double now) {
  std::vector<Island> ordered;
  for (const Island& old : g_islands) {
    for (auto it = fresh.begin(); it != fresh.end(); ++it) {
      bool shared = false;
      for (const Cell& c : it->cells) { if (std::binary_search(old.cells.begin(), old.cells.end(), c)) { shared = true; break; } }
      if (shared) { it->first_seen = old.first_seen; ordered.push_back(std::move(*it)); fresh.erase(it); break; }
    }
  }
  std::sort(fresh.begin(), fresh.end(), [](const Island& a, const Island& b) { return a.walk < b.walk; });
  for (Island& f : fresh) ordered.push_back(std::move(f));
  for (Island& is : ordered) std::sort(is.cells.begin(), is.cells.end());
  for (size_t i = 1; i < ordered.size(); ++i)   // insertion with tolerance: overtake only by a clear margin
    for (size_t j = i; j > 0 && ordered[j].walk + g_hyst < ordered[j - 1].walk; --j) std::swap(ordered[j], ordered[j - 1]);
  if ((int)ordered.size() > g_cap) ordered.resize(g_cap);
  g_islands = std::move(ordered);
  if (g_cycle >= (int)g_islands.size()) g_cycle = 0;
  (void)now;
}
void pulse_island(const Island& is, const world::Vec3& p) {
  float dx = (float)is.entry.x + 0.5f - p.x, dz = (float)is.entry.z + 0.5f - p.z;
  float len = std::sqrt(dx * dx + dz * dz);
  if (len < 1e-3f) return;
  float east = dx / len, north = -dz / len;   // north = -z (the mod's compass)
  float freq = g_pitch_lo * std::pow(g_pitch_hi / g_pitch_lo, (north + 1.0f) * 0.5f);
  audio::pulse(freq, g_pulse_ms, g_pulse_vol, east);
}
std::string clock_of(float dx, float dz) {
  float a = std::atan2(dx, -dz) * 180.0f / 3.14159265f; if (a < 0) a += 360.0f;
  int h = (int)std::lround(a / 30.0f); if (h == 0) h = 12;
  return std::format("{} o'clock", h);
}
}  // namespace

void silence() {
  for (int i = 0; i < 4; ++i) audio::set_loop_volume(kLaneLoopId + i, 0.0f);
  audio::set_loop_volume(kBedLoopId, 0.0f); audio::set_loop_volume(kBedLoopId + 1, 0.0f);
}

void tick() {
  if (!g_enabled || !world::in_world()) { silence(); return; }
  world::Vec3 p;
  if (!world::player_position(p)) { silence(); return; }
  ensure_loaded();
  HWND fg = GetForegroundWindow();
  bool audible = fg && fg == FindWindowA("Grim Dawn", nullptr);
  double now = app::now();
  // lanes
  for (int i = 0; i < 4; ++i) {
    float d = lane_distance(i, p);
    g_dist[i] = d;
    float v = d >= g_range ? 0.0f : 1.0f - d / g_range;
    audio::set_loop_volume(kLaneLoopId + i, audible ? v * v * g_gain : 0.0f);
  }
  // bed
  bool inside = world::hazard_at(p, &g_rate, &g_type);
  if (inside != g_inside) { g_inside = inside; g_islands.clear(); g_cycle = 0; g_last_search = 0; g_next_pulse = now + 0.2; }
  float bed = inside && audible ? g_bed_gain : 0.0f;
  audio::set_loop_volume(kBedLoopId, bed); audio::set_loop_volume(kBedLoopId + 1, bed);
  if (!inside) return;
  // pointer
  if (now - g_last_search >= g_search_s) { g_last_search = now; merge_islands(find_islands(p, now), now); }
  if (!g_islands.empty() && now >= g_next_pulse) {
    if (audible) pulse_island(g_islands[g_cycle], p);
    ++g_cycle;
    if (g_cycle >= (int)g_islands.size()) { g_cycle = 0; g_next_pulse = now + g_period + g_gap; }
    else g_next_pulse = now + g_period;
  }
}

std::string status() {
  world::Vec3 p; world::player_position(p);
  std::string out = std::format("enabled={} inside={} rate={:.2f} type={} lanes: north={:.1f} east={:.1f} south={:.1f} west={:.1f} (range {:.1f}, {} lanes, vol {:.2f})\n"
                                "bed vol {:.2f} spread {:.2f}; pointer period {:.2f} gap {:.2f} cap {} radius {} pulse {} ms vol {:.2f} pitch {:.0f}..{:.0f} hyst {:.1f} search {:.2f}s\n",
                                g_enabled, g_inside, g_rate, g_type, g_dist[0], g_dist[1], g_dist[2], g_dist[3], g_range, 2 * g_lanes + 1, g_gain,
                                g_bed_gain, g_spread, g_period, g_gap, g_cap, g_radius, g_pulse_ms, g_pulse_vol, g_pitch_lo, g_pitch_hi, g_hyst, g_search_s);
  out += std::format("islands: {}\n", g_islands.size());
  for (size_t i = 0; i < g_islands.size(); ++i) {
    const Island& is = g_islands[i];
    float dx = (float)is.entry.x + 0.5f - p.x, dz = (float)is.entry.z + 0.5f - p.z;
    out += std::format("  {}: entry ({}, {}) walk {} cells, {:.1f} away at {}, {} cells of ground\n", i + 1, is.entry.x, is.entry.z, is.walk, std::sqrt(dx * dx + dz * dz), clock_of(dx, dz), is.cells.size());
  }
  return out;
}

bool set_param(const std::string& k, const std::string& v) {
  float f = (float)atof(v.c_str()); int n = atoi(v.c_str());
  if (k == "on") { g_enabled = v == "1" || v == "true" || v == "on"; if (!g_enabled) silence(); }
  else if (k == "range") g_range = f > 0.5f ? f : g_range;
  else if (k == "lanes") g_lanes = n < 0 ? 0 : n > 6 ? 6 : n;
  else if (k == "vol") g_gain = f < 0 ? 0 : f > 1 ? 1 : f;
  else if (k == "bedvol") g_bed_gain = f < 0 ? 0 : f > 1 ? 1 : f;
  else if (k == "spread") g_spread = f < 0 ? 0 : f > 1 ? 1 : f;
  else if (k == "period") g_period = f > 0.05f ? f : g_period;
  else if (k == "gap") g_gap = f < 0 ? 0 : f;
  else if (k == "cap") g_cap = n < 1 ? 1 : n > 8 ? 8 : n;
  else if (k == "radius") g_radius = n < 3 ? 3 : n > 30 ? 30 : n;
  else if (k == "pvol") g_pulse_vol = f < 0 ? 0 : f > 1 ? 1 : f;
  else if (k == "pms") g_pulse_ms = n < 20 ? 20 : n > 1000 ? 1000 : n;
  else if (k == "lo") g_pitch_lo = f > 20 ? f : g_pitch_lo;
  else if (k == "hi") g_pitch_hi = f > 20 ? f : g_pitch_hi;
  else if (k == "hyst") g_hyst = f < 0 ? 0 : f;
  else if (k == "search") g_search_s = f > 0.05f ? f : g_search_s;
  else return false;
  return true;
}

std::string probe_timing(int iters) {
  if (iters < 1) iters = 1;
  world::Vec3 p;
  if (!world::in_world() || !world::player_position(p)) return "not in world\n";
  LARGE_INTEGER freq, t0, t1, t2;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t0);
  for (int n = 0; n < iters; ++n) for (int i = 0; i < 4; ++i) lane_distance(i, p);
  QueryPerformanceCounter(&t1);
  size_t cells = 0, islands = 0;
  for (int n = 0; n < iters; ++n) { auto isl = find_islands(p, 0); islands = isl.size(); cells = 0; for (auto& is : isl) cells += is.cells.size(); }
  QueryPerformanceCounter(&t2);
  double lanes_us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)freq.QuadPart / iters;
  double search_us = (double)(t2.QuadPart - t1.QuadPart) * 1e6 / (double)freq.QuadPart / iters;
  return std::format("lanes {:.1f} us/frame; island search {:.1f} us (radius {}, {} islands, {} clean cells) x{}\n", lanes_us, search_us, g_radius, islands, cells, iters);
}
}  // namespace gd::hazard
