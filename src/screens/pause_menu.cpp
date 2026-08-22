#include "screens/pause_menu.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "screens/controls.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "pause menu";

class PauseMenuScreen : public Screen {
 public:
  std::string_view key() const override { return "pause_menu"; }
  bool is_active() override { return textcap::has_text("Return to Game") && textcap::has_text("Quit to Desktop"); }
  std::string screen_name() const override { return std::string(strings::kPauseMenu); }
  int layer() const override { return 25; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { click_label(kScreen, "Return to Game"); }}};
  }
  void build(GraphBuilder& b) override {
    b.begin_stop("menu");
    for (const char* item : {"Return to Game", "Options Menu", "Exit to Main Menu", "Quit to Desktop"})
      b.add_item(ControlId::structural(std::string("pause.") + item), click_button(kScreen, item));
  }
};

std::unique_ptr<Screen> make_pause_menu() { return std::make_unique<PauseMenuScreen>(); }
}  // namespace gd::screens
