#include "screens/message_box.h"
#include <cstdlib>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "message box";
// The box is centred; its text sits within this many pixels of the button column and this far above it
// (measured on the 1600x900 "Welcome to Grim Dawn v1.3" box: text x 683..796 around Okay at 802, y 292..380
// above Okay at 586).
constexpr int kHalfWidth = 220, kHeight = 420;

static const ControlType kTextType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};

class MessageBoxScreen : public Screen {
 public:
  std::string_view key() const override { return "message_box"; }
  bool is_active() override {
    // Okay-style box, or a Yes/No question. (The main menu's own dialogs have their own screens.)
    return textcap::has_text("Okay") || (textcap::has_text("Yes") && textcap::has_text("No"));
  }
  std::string screen_name() const override { return std::string(strings::kMessage); }
  int layer() const override { return 30; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [this] { click_label(kScreen, yes_no() ? "No" : "Okay", 0, 0, true); }}};
  }

  void build(GraphBuilder& b) override {
    b.begin_stop("message");
    {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kTextType;
      v->announcements = {NodeAnnouncement([this] { return message(); }, false, announcement_kinds::kValue)};
      b.add_item(ControlId::structural("message_box.text"), v);
    }
    b.begin_stop("buttons");
    b.start_row("buttons");
    if (yes_no()) {
      b.add_item(ControlId::structural("message_box.Yes"), click_button(kScreen, "Yes"));
      b.add_item(ControlId::structural("message_box.No"), click_button(kScreen, "No"));
    } else {
      b.add_item(ControlId::structural("message_box.Okay"), click_button(kScreen, "Okay"));
    }
    b.end_row();
  }

 private:
  static bool yes_no() { return !textcap::has_text("Okay"); }
  // The game's text above the button column, top to bottom, joined as fragments.
  static std::string message() {
    textcap::Item anchor;
    if (!textcap::find_item(yes_no() ? "No" : "Okay", anchor, true)) return std::string(strings::kNoDetails);
    MessageBuilder m;
    for (const textcap::Item& it : textcap::snapshot()) {
      if (it.y >= anchor.y || it.y < anchor.y - kHeight || std::abs(it.x - anchor.x) > kHalfWidth) continue;
      std::string t = textcap::speakable(it.text);
      if (!t.empty()) m.fragment(t);
    }
    if (m.empty()) m.fragment(strings::kNoDetails);
    return m.build();
  }
};

std::unique_ptr<Screen> make_message_box() { return std::make_unique<MessageBoxScreen>(); }
}  // namespace gd::screens
