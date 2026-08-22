#include "screens/difficulty_select.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "difficulty select";
// The game greys out a locked difficulty's tile label (Elite/Ultimate until unlocked on the account).
static constexpr uint32_t kGreyed = 0x80808080;

class DifficultySelectScreen : public Screen {
 public:
  std::string_view key() const override { return "difficulty_select"; }
  bool is_active() override { return textcap::has_text("Difficulty Select"); }
  std::string screen_name() const override { return "Difficulty Select"; }
  int layer() const override { return 20; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { click_label(kScreen, textcap::has_text("Accept") ? "Cancel" : "Back", 0, 0, true); }}};
  }
  void on_push() override { selected_ = "Normal"; }  // the game opens on Normal

  void build(GraphBuilder& b) override {
    b.begin_stop("difficulty");
    b.start_row("difficulty");
    for (const char* d : {"Normal", "Veteran", "Elite", "Ultimate"}) {
      std::string name = d;
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kRadioType;
      v->announcements = {NodeAnnouncement([name] { return name; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([this, name] { return selected_ == name ? std::string(strings::kSelected) : std::string(); }, true, announcement_kinds::kSelected),
                          NodeAnnouncement([name] { return locked(name) ? std::string(strings::kDisabled) : std::string(); }, false, announcement_kinds::kEnabled)};
      v->on_activate = [this, name] {
        if (locked(name)) return;  // the state readout after activation says "disabled"
        selected_ = name;
        click_label(kScreen, name, 0, 0, true);  // the tile label (the selected one's name is also the dialog's title, higher up)
      };
      // The dialog describes the SELECTED difficulty; that text is the tile's tooltip.
      v->on_tooltip = [this, name] {
        if (selected_ != name) { speech::speak(strings::kNoTooltip, false); return; }
        speech::speak(description(), true);
      };
      v->state_text = [this, name] {
        MessageBuilder m;
        m.fragment(locked(name) ? strings::kDisabled : selected_ == name ? strings::kSelected : strings::kNotSelected);
        return m.build();
      };
      b.add_item(ControlId::structural("difficulty." + name), v);
    }
    b.end_row();
    b.begin_stop("buttons");
    b.start_row("buttons");
    // Two variants of the dialog: after Create Character its buttons are Create / Back; opened from the main
    // menu's difficulty button they are Accept / Cancel (checkpoint read at build time).
    bool from_menu = textcap::has_text("Accept");
    for (const char* btn : from_menu ? std::initializer_list<const char*>{"Accept", "Cancel"} : std::initializer_list<const char*>{"Create", "Back"}) {
      // The main menu behind the dialog also draws a "Create", slightly higher up; the dialog's is the last
      // match top-to-bottom.
      std::string label = btn;
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel)};
      v->on_activate = [label] { click_label(kScreen, label, 0, 0, true); };
      b.add_item(ControlId::structural("difficulty." + label), v);
    }
    b.end_row();
  }

 private:
  static bool locked(const std::string& name) {
    textcap::Item it;
    return textcap::find_item(name, it, true) && it.rgba == kGreyed;
  }
  // The description block: left-aligned lines in the dialog's text area, left of the portrait, below the
  // title. Game text, verbatim.
  static std::string description() {
    MessageBuilder m;
    for (const textcap::Item& it : textcap::snapshot()) {
      if (it.xalign != 0 || it.x < 560 || it.x > 870 || it.y < 300 || it.y > 520) continue;
      std::string t = textcap::speakable(it.text);
      if (!t.empty()) m.fragment(t);
    }
    if (m.empty()) m.fragment(strings::kNoDetails);
    return m.build();
  }

  std::string selected_ = "Normal";
};

std::unique_ptr<Screen> make_difficulty_select() { return std::make_unique<DifficultySelectScreen>(); }
}  // namespace gd::screens
