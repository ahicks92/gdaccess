#include "rooms.h"
#include "audio.h"
#include "db.h"
#include "log.h"
#include "speech.h"
#include "voice.h"
#include "world.h"
#include "core/message_builder.h"
#include "core/rooms_model.h"
#include "core/strings.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gd::rooms {
namespace {
using gd::core::MessageBuilder;
namespace strings = gd::strings;

struct Room { std::string key, title, body, subregion; std::string cls; float ax = 0, az = 0; bool island = false; };
struct Exit { int a = -1, b = -1; float x = 0, z = 0, width = 0; bool cut = false; };
struct Region {
  std::string key, name;
  core::rooms::LabelGrid grid;
  std::vector<Room> rooms;                 // index = grid label
  std::vector<Exit> exits;
  std::map<std::string, std::string> subregion_names;
  bool loaded = false;
};

std::unique_ptr<db::Db> g_db;
std::map<std::string, std::string> g_chunk_region;   // lvl path -> region key
std::map<std::string, std::unique_ptr<Region>> g_regions;
Region* g_current = nullptr;
std::string g_announced_region, g_announced_subregion;
core::rooms::Hysteresis g_hyst;
bool g_say_untitled = true;
std::string g_last_line;
constexpr int kLookupRing = 8;           // cells (2 units) of ring search around the player's cell

int now_ms() { return (int)(GetTickCount64() & 0x7fffffff); }

// The chunks column is a JSON array of strings; pull the quoted strings out.
std::vector<std::string> json_strings(const std::string& s) {
  std::vector<std::string> out;
  size_t p = 0;
  while ((p = s.find('"', p)) != std::string::npos) {
    size_t e = s.find('"', p + 1);
    if (e == std::string::npos) break;
    std::string v = s.substr(p + 1, e - p - 1);
    for (size_t k = v.find("\\\\"); k != std::string::npos; k = v.find("\\\\", k + 1)) v.erase(k, 1);
    out.push_back(v);
    p = e + 1;
  }
  return out;
}

bool open_db() {
  g_db = std::make_unique<db::Db>();
  std::string path = audio::module_dir() + "assets\\rooms.db";
  if (!g_db->open_readonly(path)) { g_db.reset(); log::writef("rooms: no database at {}", path); return false; }
  db::Stmt st(*g_db, "SELECT key, chunks FROM regions");
  while (st.step()) {
    std::string key = st.text(0);
    for (const std::string& c : json_strings(st.text(1))) {
      std::string n = c;
      std::replace(n.begin(), n.end(), '\\', '/');
      g_chunk_region[n] = key;
    }
  }
  log::writef("rooms: {} ({} chunks mapped, sqlite {})", path, g_chunk_region.size(), db::version());
  return true;
}

Region* load_region(const std::string& key) {
  auto it = g_regions.find(key);
  if (it != g_regions.end()) return it->second.get();
  auto r = std::make_unique<Region>();
  r->key = key;
  {
    db::Stmt st(*g_db, "SELECT name FROM regions WHERE key=?");
    st.bind(1, key);
    if (st.step()) r->name = st.text(0);
  }
  std::vector<std::string> label_keys;
  {
    db::Stmt st(*g_db, "SELECT x0, z0, w, h, cell, labels, label_keys FROM grids WHERE region_key=?");
    st.bind(1, key);
    if (!st.step()) { log::writef("rooms: region '{}' has no grid", key); return nullptr; }
    r->grid.x0 = st.real(0); r->grid.z0 = st.real(1); r->grid.cell = st.real(4);
    std::vector<uint8_t> blob = st.blob(5);
    if (!r->grid.decode_rle(blob.data(), blob.size(), (int)st.int64(2), (int)st.int64(3))) { log::writef("rooms: region '{}' grid does not decode", key); return nullptr; }
    label_keys = json_strings(st.text(6));
  }
  r->rooms.resize(label_keys.size());
  std::map<std::string, int> by_key;
  for (size_t i = 0; i < label_keys.size(); ++i) { r->rooms[i].key = label_keys[i]; by_key[label_keys[i]] = (int)i; }
  {
    db::Stmt st(*g_db, "SELECT key, title, body, subregion_key, cls, anchor_x, anchor_z, island FROM rooms WHERE region_key=?");
    st.bind(1, key);
    while (st.step()) {
      auto f = by_key.find(st.text(0));
      if (f == by_key.end()) continue;
      Room& rm = r->rooms[f->second];
      rm.title = st.text(1); rm.body = st.text(2); rm.subregion = st.text(3); rm.cls = st.text(4);
      rm.ax = (float)st.real(5); rm.az = (float)st.real(6); rm.island = st.int64(7) != 0;
    }
  }
  {
    db::Stmt st(*g_db, "SELECT room_a, room_b, x, z, width, cut FROM exits WHERE region_key=?");
    st.bind(1, key);
    while (st.step()) {
      auto a = by_key.find(st.text(0)), b = by_key.find(st.text(1));
      if (a == by_key.end() || b == by_key.end()) continue;
      r->exits.push_back({a->second, b->second, (float)st.real(2), (float)st.real(3), (float)st.real(4), st.int64(5) != 0});
    }
  }
  {
    db::Stmt st(*g_db, "SELECT key, name FROM subregions WHERE region_key=?");
    st.bind(1, key);
    while (st.step()) r->subregion_names[st.text(0)] = st.text(1);
  }
  r->loaded = true;
  log::writef("rooms: loaded region '{}' ({}): {}x{} cells, {} rooms, {} exits", key, r->name, r->grid.w, r->grid.h, r->rooms.size(), r->exits.size());
  Region* raw = r.get();
  g_regions[key] = std::move(r);
  return raw;
}

std::string room_label(const Region& r, int label) {
  if (label < 0 || label >= (int)r.rooms.size()) return {};
  const Room& rm = r.rooms[label];
  if (!rm.title.empty()) return rm.title;
  if (!g_say_untitled) return {};
  MessageBuilder m;
  m.fragment(strings::kRoom).fragment(std::to_string(label));
  return m.build();
}

void announce(bool force) {
  if (!g_current || g_hyst.current < 0) return;
  const Room& rm = g_current->rooms[g_hyst.current];
  std::string region = g_current->name.empty() ? g_current->key : g_current->name;
  auto sr = g_current->subregion_names.find(rm.subregion);
  std::string subregion = sr != g_current->subregion_names.end() ? sr->second : std::string();
  MessageBuilder m;
  strings::push_place(m, (force || region != g_announced_region) ? region : std::string_view(),
                      (force || subregion != g_announced_subregion) ? subregion : std::string_view(), room_label(*g_current, g_hyst.current));
  g_announced_region = region; g_announced_subregion = subregion;
  std::string line = m.build();
  if (line.empty()) return;
  g_last_line = line;
  voice::say({voice::Which::Zira, line, 0.0f, 1.0f, voice::Policy::Replace, voice::kGroupInfo});
}

Region* region_for_player() {
  std::string chunk = world::region_name();
  std::replace(chunk.begin(), chunk.end(), '\\', '/');
  auto it = g_chunk_region.find(chunk);
  if (it == g_chunk_region.end()) return nullptr;
  return load_region(it->second);
}
}  // namespace

std::vector<world::ScanItem> exit_items();   // below
void init() { open_db(); world::set_exit_provider(exit_items); }
void shutdown() { g_regions.clear(); g_current = nullptr; g_db.reset(); }
void reset() { g_hyst.reset(); g_announced_region.clear(); g_announced_subregion.clear(); g_current = nullptr; }
void set_dwell_ms(int ms) { g_hyst.dwell_ms = ms; }
void set_settle_ms(int ms) { g_hyst.settle_ms = ms; }
void set_say_untitled(bool on) { g_say_untitled = on; }
void announce_now() { announce(true); }
void reload() { shutdown(); g_chunk_region.clear(); reset(); open_db(); }

unsigned g_ticks = 0;
void tick() {
  ++g_ticks;
  if (!g_db || !world::in_world()) return;
  world::Vec3 p;
  if (!world::player_position(p)) return;
  Region* r = region_for_player();
  if (r != g_current) { g_current = r; g_hyst.reset(); }
  if (!r) return;
  // The runtime mesh is up to ~1.5 units wider than the bake at edges (measured 2026-08-22 north of the
  // spawn: ours from x 62.45, the game from 61.0), so search 8 cells = 2 units around the player.
  int label = r->grid.label_at(p.x, p.z, kLookupRing);
  if (g_hyst.update(label, now_ms())) announce(false);
}

void speak_description() {
  if (!g_current || g_hyst.current < 0) { speech::speak(strings::kNoRoom, true); return; }
  const Room& rm = g_current->rooms[g_hyst.current];
  MessageBuilder m;
  m.list_item().fragment(room_label(*g_current, g_hyst.current));
  m.list_item().fragment(rm.body.empty() ? std::string(strings::kNoDescription) : rm.body);
  speech::speak(m.build(), true);
}

// An exit is "blocked" when the live mesh refuses the opening although the bake allows it (a runtime
// obstacle: door, barricade, gate). The live mesh is up to ~1.5 units narrower than the bake at edges, so one
// point is not enough: probe a cross and call it blocked only if every point fails.
bool exit_blocked(const world::Vec3& at) {
  const float d = 0.75f;
  const world::Vec3 pts[] = {at, {at.x + d, at.y, at.z}, {at.x - d, at.y, at.z}, {at.x, at.y, at.z + d}, {at.x, at.y, at.z - d}};
  for (const world::Vec3& p : pts) if (world::on_navmesh(p)) return false;
  return true;
}

// The scanner's exit group: the current room's exits as point items. id = kPointIdBase + the region's exit
// index (stable, so the cycle continues from the reviewed exit); label = the destination's title or "room N".
std::vector<world::ScanItem> exit_items() {
  std::vector<world::ScanItem> out;
  world::Vec3 p;
  if (!g_current || g_hyst.current < 0 || !world::player_position(p)) return out;
  int room = g_hyst.current;
  for (int i = 0; i < (int)g_current->exits.size(); ++i) {
    const Exit& e = g_current->exits[i];
    if (e.a != room && e.b != room) continue;
    int other = e.a == room ? e.b : e.a;
    world::Vec3 at{e.x, p.y, e.z};
    std::string dest = room_label(*g_current, other);
    if (dest.empty()) dest = other >= 0 && other < (int)g_current->rooms.size() ? g_current->rooms[other].cls : std::string(strings::kRoom);
    out.push_back({world::kPointIdBase + (unsigned)i, "exit", dest, {}, at, 0.0f, exit_blocked(at) ? std::string(strings::kBlocked) : std::string()});
  }
  return out;
}

std::string status() {
  if (!g_db) return "rooms: no database\n";
  std::string s = std::format("db open, {} chunks mapped, {} regions loaded, dwell {} ms, settle {} ms, untitled {}\n", g_chunk_region.size(), g_regions.size(), g_hyst.dwell_ms, g_hyst.settle_ms, g_say_untitled);
  for (const auto& [chunk, key] : g_chunk_region) s += std::format("  '{}' -> {}\n", chunk, key);
  s += std::format("ticks {}\n", g_ticks);
  world::Vec3 p;
  bool have = world::in_world() && world::player_position(p);
  s += std::format("chunk '{}' -> region {}\n", world::region_name(), g_current ? g_current->key : "none");
  if (!g_current || !have) return s;
  int label = g_current->grid.label_at(p.x, p.z, kLookupRing);
  s += std::format("player ({:.1f}, {:.1f}) -> label {} current {} candidate {}; last line '{}'\n", p.x, p.z, label, g_hyst.current, g_hyst.candidate, g_last_line);
  if (g_hyst.current >= 0) {
    const Room& rm = g_current->rooms[g_hyst.current];
    s += std::format("room key {} cls {} anchor ({:.0f}, {:.0f}) island {} title '{}' subregion '{}' body '{}'\n", rm.key, rm.cls, rm.ax, rm.az, rm.island, rm.title, rm.subregion, rm.body.substr(0, 80));
    for (const Exit& e : g_current->exits)
      if (e.a == g_hyst.current || e.b == g_hyst.current) {
        int other = e.a == g_hyst.current ? e.b : e.a;
        world::Vec3 at{e.x, p.y, e.z};
        s += std::format("  exit -> {} at ({:.1f}, {:.1f}) width {:.1f} cut {} dist {:.1f} hour {} walkable {}\n", other, e.x, e.z, e.width, e.cut, std::hypot(e.x - p.x, e.z - p.z), world::clock_hour(at), world::on_navmesh(at));
      }
  }
  return s;
}
}  // namespace gd::rooms
