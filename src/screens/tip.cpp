#include "screens/tip.h"
#include <format>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "speech.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;

static const ControlType kLineType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};

// A tutorial tip, read from the game's tip manager (exe_ui::tips): its lines are the localized text already
// split by the game (line 0 = title). Shown as a mod-owned overlay while the tip is up; Close dismisses it the
// way a right click does. Notifications (kind 0) are not tips and stay with the game.
static exe_ui::Tip current_tip() {
  for (exe_ui::Tip t : exe_ui::tips()) if (t.kind() == 1 && t.showing()) return t;
  return {};
}
static std::vector<std::string> lines_of(const exe_ui::Tip& t) {
  std::vector<std::string> out;
  for (const std::string& l : t.lines()) { std::string s = textcap::speakable(l); if (!s.empty()) out.push_back(s); }
  return out;
}

class TipScreen : public Screen {
 public:
  std::string_view key() const override { return "tip"; }
  bool is_active() override { return exe_ui::available() && (bool)current_tip(); }
  std::string screen_name() const override { return std::string(strings::kTip); }
  int layer() const override { return 35; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { close(); }}}; }
  void on_focus() override {
    Screen::on_focus();
    // Read the whole tip at once on arrival; the items below let the player re-read line by line.
    exe_ui::Tip t = current_tip();
    if (!t) return;
    MessageBuilder m;
    for (const std::string& l : lines_of(t)) m.fragment(l);
    speech::speak(m.build(), false);
  }
  void build(GraphBuilder& b) override {
    exe_ui::Tip t = current_tip();
    if (!t) return;
    b.begin_stop("tip");
    std::vector<std::string> lines = lines_of(t);
    for (size_t i = 0; i < lines.size(); ++i) {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kLineType;
      std::string text = lines[i];
      v->announcements = {NodeAnnouncement([text] { return text; }, false, announcement_kinds::kValue)};
      b.add_item(ControlId::structural(std::format("tip.line{}", i)), v);
    }
    b.begin_stop("buttons");
    {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([] { return std::string(strings::kClose); }, false, announcement_kinds::kLabel)};
      v->on_activate = [] { close(); };
      b.add_item(ControlId::structural("tip.close"), v);
    }
  }

 private:
  static void close() { exe_ui::Tip t = current_tip(); if (t) t.dismiss(); }
};

std::unique_ptr<Screen> make_tip() { return std::make_unique<TipScreen>(); }
}  // namespace gd::screens
