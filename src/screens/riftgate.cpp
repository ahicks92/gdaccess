#include "screens/riftgate.h"
#include <algorithm>
#include <format>
#include <map>
#include "hooks.h"
#include "riftgate_zones.h"
#include "screens/window_base.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace {
// The map draws its gates in image-tile order, which is arbitrary for a listener. The game does have an order:
// the master table's zone list (src/riftgate_zones.h, generated) runs by act and then story progression, and
// the acts come from the zone records' chunk letters. A live icon is matched to a zone by its localized name.
struct ZoneInfo { int act = 0; int order = 1000; };
const std::map<std::string, ZoneInfo>& zone_by_name() {
  static std::map<std::string, ZoneInfo> cache;
  if (cache.empty()) {   // localize() is empty until the game has localized anything: retry until it fills
    for (const riftgate_zones::Zone& z : riftgate_zones::kZones) {
      std::string name = hooks::localize(z.tag);
      if (!name.empty()) cache[name] = ZoneInfo{z.act, z.order};
    }
  }
  return cache;
}
bool is_devils_crossing(const exe_ui::Riftgate& g) {
  static std::string dc;
  if (dc.empty()) dc = hooks::localize("tagRiftDevilsCrossing");
  return !dc.empty() && g.name == dc;
}
float flat_distance(const world::Vec3& a, const world::Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
}  // namespace

// The game's riftgate travel UI is the world map with the discovered gates as icons (WorldMapWindow inside
// the MiniMap window). We list what it draws and travel through the same two calls its click handler makes;
// the map stays the game's (opens from a rift or the L key, the close button / Escape hides it). Tab stops:
// "all" = personal rift(s), then Devil's Crossing (the town you keep hopping back to), then every other
// discovered gate nearest first; one stop per act in the game's own progression order. (The discovered
// devotion shrines live on the Ctrl+M map screen, screens/shrine_list.h.)
class RiftgateScreen : public WindowScreen {
 public:
  RiftgateScreen() : WindowScreen("riftgate", std::string(strings::kRiftgates), exe_ui::ingame::kMiniMap, 13) {}
  bool is_active() override { return exe_ui::riftgate_map_open(); }
  void close() override { exe_ui::riftgate_map_close(); }
  void build(GraphBuilder& b) override {
    const std::vector<exe_ui::Riftgate>& gates = gates_.get([] { return exe_ui::riftgates(); }, 30);
    world::Vec3 me{}; bool have_me = world::player_position(me);
    const std::map<std::string, ZoneInfo>& zones = zone_by_name();

    struct Row { size_t index; float distance; int act; int order; bool personal; bool town; };
    std::vector<Row> rows;
    for (size_t i = 0; i < gates.size(); ++i) {
      const exe_ui::Riftgate& g = gates[i];
      world::Vec3 p{(float)g.pos[0], (float)g.pos[1], (float)g.pos[2]};
      auto z = zones.find(g.name);
      rows.push_back({i, have_me ? flat_distance(p, me) : 0.f, z == zones.end() ? 0 : z->second.act, z == zones.end() ? 1000 : z->second.order,
                      g.owner != 0, is_devils_crossing(g)});
    }

    // ---- all: personal, then town, then nearest first ----
    b.begin_stop("all");
    b.push_context(strings::kAllRiftgates, strings::kList);
    if (rows.empty()) b.add_item(ControlId::structural("riftgate.none"), line_item(std::string(strings::kNoRiftgates)));
    std::vector<Row> all = rows;
    std::stable_sort(all.begin(), all.end(), [](const Row& a, const Row& c) {
      if (a.personal != c.personal) return a.personal;
      if (a.town != c.town) return a.town;
      return a.distance < c.distance;
    });
    for (const Row& r : all) add_gate(b, gates[r.index], r.index, "all", me, have_me);
    b.pop_context();

    // ---- one stop per act, in the game's order ----
    for (int act = 1; act <= riftgate_zones::kActs; ++act) {
      std::vector<Row> in_act;
      for (const Row& r : rows) if (r.act == act) in_act.push_back(r);
      if (in_act.empty()) continue;
      std::stable_sort(in_act.begin(), in_act.end(), [](const Row& a, const Row& c) { return a.order < c.order; });
      MessageBuilder title; title.fragment(std::string(strings::kAct)).fragment(std::to_string(act));
      b.begin_stop(std::format("act{}", act));
      b.push_context(title.build(), strings::kList);
      for (const Row& r : in_act) add_gate(b, gates[r.index], r.index, std::format("act{}", act), me, have_me);
      b.pop_context();
    }
  }

 private:
  void add_gate(GraphBuilder& b, const exe_ui::Riftgate& g, size_t index, const std::string& stop, const world::Vec3& me, bool have_me) {
    std::string label = g.name.empty() ? std::format("riftgate {}", index + 1) : g.name;
    // The value: "you are here", else the distance and clock bearing like the review cursor's readouts.
    std::function<std::string()> value;
    if (g.current) value = [] { return std::string(strings::kYouAreHere); };
    else if (have_me) {
      world::Vec3 p{(float)g.pos[0], (float)g.pos[1], (float)g.pos[2]};
      float d = flat_distance(p, me);
      int hour = world::clock_hour(p);
      value = [d, hour] { MessageBuilder m; strings::push_distance_bearing(m, d, hour); return m.build(); };
    }
    exe_ui::Riftgate copy = g;
    b.add_item(ControlId::structural(std::format("riftgate.{}.{}", stop, index)),   // by index: the map may hold twins of one gate
               row_item(label, value, [this, copy] {
                 if (copy.current) { speech::speak(strings::kYouAreHere, true); return; }
                 if (exe_ui::riftgate_travel(copy)) { gates_.invalidate(); }
               }));
  }
  Snapshot<std::vector<exe_ui::Riftgate>> gates_;
};

std::unique_ptr<Screen> make_riftgate() { return std::make_unique<RiftgateScreen>(); }
}  // namespace gd::screens
