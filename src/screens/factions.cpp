#include "screens/factions.h"
#include <format>
#include "gameapi.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

class FactionsScreen : public WindowScreen {
 public:
  FactionsScreen() : WindowScreen("factions", std::string(strings::kFactions), exe_ui::ingame::kFactions, 13) {}
  void build(GraphBuilder& b) override {
    const std::vector<gameapi::Faction>& fs = factions_.get([] { return gameapi::factions(); }, 60);
    b.begin_stop("list");
    bool any = false;
    for (const gameapi::Faction& f : fs) {
      any = true;
      MessageBuilder m;
      strings::push_faction(m, f.name.empty() ? f.tag : f.name, f.level_name, f.value, f.low, f.high);
      b.add_item(ControlId::structural(std::format("factions.{}", f.type)), line_item(m.build()));
    }
    if (!any) b.add_item(ControlId::structural("factions.none"), line_item(std::string(strings::kNoFactions)));
  }
 private:
  Snapshot<std::vector<gameapi::Faction>> factions_;
};

std::unique_ptr<Screen> make_factions() { return std::make_unique<FactionsScreen>(); }
}  // namespace gd::screens
