#include "screens/map_markers.h"
#include <cmath>
#include <format>
#include "screens/window_base.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

// Ctrl+M injects the game's M, which opens the local aerial map; this screen becomes current while that map
// is open (exe_ui::aerial_map_open) and reads the map's own icon set (world::map_markers, filled live while
// the map is shown). Two tabs split the icons the way the design calls for: quest markers vs everything else
// (merchants, riftgate, spirit guide, NPCs, ...). Activating a row picks it as the follow target and closes
// the map; the ' key then pings it with distance and heading.
class MapMarkersScreen : public WindowScreen {
 public:
  MapMarkersScreen() : WindowScreen("mapmarkers", std::string(strings::kMapMarkers), exe_ui::ingame::kMiniMap, 13) {}
  bool is_active() override { return exe_ui::aerial_map_open(); }
  void close() override { exe_ui::aerial_map_close(); }
  void on_tab_changed(int) override { markers_.invalidate(); }

  void build(GraphBuilder& b) override {
    add_tabs(b, {std::string(strings::kQuestMarkers), std::string(strings::kMapPoints)});
    const std::vector<world::MapMarker>& all = markers_.get([] { return world::map_markers(); }, 15);
    bool want_quest = tab() == 0;
    world::Vec3 me{};
    bool have_me = world::player_position(me);
    b.begin_stop("list");
    size_t shown = 0;
    for (size_t i = 0; i < all.size(); ++i) {
      const world::MapMarker& mk = all[i];
      if (mk.quest != want_quest) continue;
      ++shown;
      std::string label = mk.label.empty() ? std::string(strings::kMapPoints) : mk.label;
      std::function<std::string()> value;
      if (have_me) {
        world::Vec3 p = mk.pos;
        float d = std::sqrt((p.x - me.x) * (p.x - me.x) + (p.z - me.z) * (p.z - me.z));
        int hour = world::clock_hour(p);
        value = [d, hour] { MessageBuilder m; strings::push_distance_bearing(m, d, hour); return m.build(); };
      }
      world::MapMarker copy = mk;
      b.add_item(ControlId::structural(std::format("marker.{}", i)),
                 row_item(label, value, [this, copy] {
                   world::set_follow_target(copy.id, copy.pos, copy.label);
                   MessageBuilder m;
                   m.fragment(strings::kFollowing).fragment(copy.label);
                   speech::speak(m.build(), true);
                   close();   // return to the world; ' now follows the picked marker
                 }));
    }
    if (shown == 0)
      b.add_item(ControlId::structural("marker.none"),
                 line_item(std::string(want_quest ? strings::kNoQuestMarkers : strings::kNoMarkersHere)));
  }

 private:
  Snapshot<std::vector<world::MapMarker>> markers_;
};

std::unique_ptr<Screen> make_map_markers() { return std::make_unique<MapMarkersScreen>(); }
}  // namespace gd::screens
