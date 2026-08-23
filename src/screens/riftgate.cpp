#include "screens/riftgate.h"
#include <format>
#include "screens/window_base.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

// The game's riftgate travel UI is the world map with the discovered gates as icons (WorldMapWindow inside
// the MiniMap window). We list what it draws -- the discovered gates in the map's own order, the one you
// stand at marked -- and travel through the same two calls its click handler makes. The map stays the
// game's: it opens from a rift or the L key and the close button / Escape hides it.
class RiftgateScreen : public WindowScreen {
 public:
  RiftgateScreen() : WindowScreen("riftgate", std::string(strings::kRiftgates), exe_ui::ingame::kMiniMap, 13) {}
  bool is_active() override { return exe_ui::riftgate_map_open(); }
  void close() override { exe_ui::riftgate_map_close(); }
  void build(GraphBuilder& b) override {
    const std::vector<exe_ui::Riftgate>& gates = gates_.get([] { return exe_ui::riftgates(); }, 30);
    b.begin_stop("list");
    if (gates.empty()) { b.add_item(ControlId::structural("riftgate.none"), line_item(std::string(strings::kNoRiftgates))); return; }
    world::Vec3 me{}; bool have_me = world::player_position(me);
    for (size_t i = 0; i < gates.size(); ++i) {
      const exe_ui::Riftgate& g = gates[i];
      std::string label = g.name.empty() ? std::format("riftgate {}", i + 1) : g.name;
      // The value: "you are here", else the distance and clock bearing like the review cursor's readouts.
      std::function<std::string()> value;
      if (g.current) value = [] { return std::string(strings::kYouAreHere); };
      else if (have_me) {
        world::Vec3 p{(float)g.pos[0], (float)g.pos[1], (float)g.pos[2]};
        float d = std::sqrt((p.x - me.x) * (p.x - me.x) + (p.z - me.z) * (p.z - me.z));
        int hour = world::clock_hour(p);
        value = [d, hour] { MessageBuilder m; strings::push_distance_bearing(m, d, hour); return m.build(); };
      }
      exe_ui::Riftgate copy = g;
      b.add_item(ControlId::structural(std::format("riftgate.{}", i)),   // by index: the map may hold twins of one gate
                 row_item(label, value, [this, copy] {
                   if (copy.current) { speech::speak(strings::kYouAreHere, true); return; }
                   if (exe_ui::riftgate_travel(copy)) { gates_.invalidate(); }
                 }));
    }
  }
 private:
  Snapshot<std::vector<exe_ui::Riftgate>> gates_;
};

std::unique_ptr<Screen> make_riftgate() { return std::make_unique<RiftgateScreen>(); }
}  // namespace gd::screens
