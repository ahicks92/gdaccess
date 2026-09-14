#include "screens/cue_settings.h"
#include <string>
#include "core/graph_builder.h"
#include "core/strings.h"
#include "cues.h"
#include "screens/window_base.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;
namespace {
bool g_open = false;

struct CueRow { const char* id; cues::Cue cue; std::string_view label; };
const CueRow kCueRows[] = {
  {"walls", cues::WallTones, strings::kCueWallTones},
  {"hazard.lanes", cues::HazardLanes, strings::kCueHazardLanes},
  {"hazard.inside", cues::HazardInside, strings::kCueHazardInside},
  {"hazard.exit", cues::HazardExit, strings::kCueHazardExit},
  {"enemies", cues::Enemies, strings::kCueEnemies},
  {"loot", cues::Loot, strings::kCueLoot},
  {"entrances", cues::Entrances, strings::kCueEntrances},
  {"breakables", cues::Breakables, strings::kCueBreakables},
  {"shrines", cues::Shrines, strings::kCueShrines},
  {"interactables", cues::Interactables, strings::kCueInteractables},
};
struct VolumeRow { const char* id; cues::Channel channel; std::string_view label; };
const VolumeRow kVolumeRows[] = {
  {"walls", cues::Walls, strings::kVolumeWalls},
  {"hazards", cues::Hazards, strings::kVolumeHazards},
  {"enemies", cues::EnemyChannel, strings::kVolumeEnemies},
  {"other", cues::Other, strings::kVolumeOther},
};

void step_volume(cues::Channel ch, int sign) {   // Left/Right: 10 % steps, clamped; the navigator speaks the new value
  cues::set_volume(ch, cues::volume(ch) + sign * cues::kVolumeStep);
}

class CueSettingsScreen : public Screen {
 public:
  std::string_view key() const override { return "cue_settings"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kCueSettings); }
  int layer() const override { return 1; }   // like the T overlay: a game window covers and closes it
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }
  void on_unfocus() override { g_open = false; }

  void build(GraphBuilder& b) override {
    b.begin_stop("cues");
    b.push_context(strings::kCueSwitches, strings::kList);
    for (const CueRow& r : kCueRows) {
      cues::Cue c = r.cue;
      b.add_item(ControlId::structural(std::string("cue.") + r.id),
                 row_item(std::string(r.label), [c] { return std::string(cues::enabled(c) ? strings::kOn : strings::kOff); },
                          [c] { cues::set_enabled(c, !cues::enabled(c)); }));   // Enter: flip + persist
    }
    b.pop_context();

    b.begin_stop("volumes");
    b.push_context(strings::kCueVolumes, strings::kList);
    for (const VolumeRow& r : kVolumeRows) {
      cues::Channel ch = r.channel;
      auto row = row_item(std::string(r.label), [ch] { return strings::percent(cues::volume(ch)); },
                          [ch] {   // Enter: the next step up, wrapping to 0 after 100 (Left/Right are the usual way)
                            int v = cues::volume(ch) + cues::kVolumeStep;
                            cues::set_volume(ch, v > 100 ? 0 : v);
                          });
      row->on_adjust = [ch](int sign, bool) { step_volume(ch, sign); };
      b.add_item(ControlId::structural(std::string("volume.") + r.id), row);
    }
    b.pop_context();
  }
};
}  // namespace

void open_cue_settings() { g_open = true; }
std::unique_ptr<Screen> make_cue_settings_overlay() { return std::make_unique<CueSettingsScreen>(); }
}  // namespace gd::screens
