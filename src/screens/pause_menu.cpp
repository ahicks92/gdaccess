#include "screens/pause_menu.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::ExitWindow;
using exe_ui::WidgetB;

// The in-world Escape menu, read from InGameUI's exit window: showing when the window's IsVisible() says so,
// its four TextButtons carry the game's captions, and a press goes through the window's listener registry
// (the same dispatch its own mouse path ends in).
class PauseMenuScreen : public Screen {
 public:
  std::string_view key() const override { return "pause_menu"; }
  bool is_active() override { ExitWindow w = exe_ui::exit_window(); return exe_ui::available() && w && w.visible(); }
  std::string screen_name() const override { return std::string(strings::kPauseMenu); }
  int layer() const override { return 25; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { ExitWindow w = exe_ui::exit_window(); if (w) w.press(w.button(ExitWindow::kResume)); }}};
  }
  void build(GraphBuilder& b) override {
    ExitWindow w = exe_ui::exit_window();
    if (!w) return;
    b.begin_stop("menu");
    int i = 0;
    for (WidgetB btn : w.buttons()) {
      std::string label = btn.text();
      bool enabled = btn.enabled();
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([enabled] { return enabled ? std::string() : std::string(strings::kDisabled); }, true, announcement_kinds::kEnabled)};
      v->on_activate = [w, btn, enabled] { if (!enabled) { speech::speak(strings::kDisabled, true); return; } w.press(btn); };
      b.add_item(ControlId::structural(std::format("pause.{}", i++)), v);
    }
  }
};

std::unique_ptr<Screen> make_pause_menu() { return std::make_unique<PauseMenuScreen>(); }
}  // namespace gd::screens
