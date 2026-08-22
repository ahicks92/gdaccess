#include "screens/create_character.h"
#include "app.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/navigator.h"
#include "core/strings.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "create character";

namespace keys {
constexpr int Escape = 0x01, Tab = 0x0f, Enter = 0x1c, Backspace = 0x0e, Up = 0x79, Down = 0x7e;
}

// Drop the last UTF-8 character; returns it.
static std::string pop_utf8(std::string& s) {
  if (s.empty()) return {};
  size_t i = s.size() - 1;
  while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) --i;
  std::string last = s.substr(i);
  s.erase(i);
  return last;
}

class CreateCharacterScreen : public Screen {
 public:
  std::string_view key() const override { return "create_character"; }
  bool is_active() override { return textcap::has_text("Create Character") && textcap::has_text("Hardcore"); }
  std::string screen_name() const override { return "Create Character"; }
  int layer() const override { return 20; }
  bool exclusive() const override { return true; }
  // Editing the name: the game's own field gets the typing, we read the keys ourselves (no navigation
  // dispatch, no type-ahead) and leave on Enter/Escape/Tab/arrows.
  bool captures_raw_input() const override { return editing_; }
  bool allows_typeahead() const override { return !editing_; }
  bool passes_key(int code) const override {
    if (!editing_) return false;
    return code != keys::Escape && code != keys::Enter && code != keys::Tab && code != keys::Up && code != keys::Down;
  }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { click_label(kScreen, "Cancel"); }}};
  }
  // The dialog comes back with its fields intact from Difficulty Select's Back, and cleared after Cancel
  // (measured 2026-08-21): keep our state across a Next, reset it otherwise.
  void on_push() override {
    if (!submitted_) { male_ = true; hardcore_ = false; name_.clear(); }
    submitted_ = false;
    editing_ = false;
  }
  void on_unfocus() override { editing_ = false; }

  void on_update() override {
    if (!editing_) return;
    const KeySource& ks = hooks::key_source();
    for (char16_t c : hooks::typed_chars()) {
      std::string ch = log::utf8(std::u16string_view(&c, 1));
      name_ += ch;
      speech::speak(c == u' ' ? std::string(strings::kSpace) : ch, true);
    }
    if (ks.just_pressed(keys::Backspace)) {
      std::string gone = pop_utf8(name_);
      speech::speak(gone.empty() ? std::string(strings::kEmpty) : gone == " " ? std::string(strings::kSpace) : gone, true);
    }
    GraphNavigator* nav = app::navigator();
    if (ks.just_pressed(keys::Enter) || ks.just_pressed(keys::Escape)) stop_editing();
    else if (ks.just_pressed(keys::Tab)) { stop_editing(); if (nav) nav->on_action(ks.shift() ? ui_actions::Prev : ui_actions::Next); }
    else if (ks.just_pressed(keys::Up)) { stop_editing(); if (nav) nav->on_action(ui_actions::Up); }
    else if (ks.just_pressed(keys::Down)) { stop_editing(); if (nav) nav->on_action(ui_actions::Down); }
  }

  void build(GraphBuilder& b) override {
    b.begin_stop("form");
    {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kEditType;
      v->announcements = {NodeAnnouncement([] { return std::string("Name"); }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([this] { return name_.empty() ? std::string(strings::kEmpty) : name_; }, false, announcement_kinds::kValue)};
      v->on_activate = [this] { if (editing_) stop_editing(); else start_editing(); };
      b.add_item(ControlId::structural("create.name"), v);
    }
    b.push_context("Sex", strings::kList);
    for (const char* sex : {"Male", "Female"}) {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kRadioType;
      std::string label = sex;
      bool is_male = label == "Male";
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([this, is_male] { return male_ == is_male ? std::string(strings::kSelected) : std::string(); }, true, announcement_kinds::kSelected)};
      v->on_activate = [this, label, is_male] { male_ = is_male; click_label(kScreen, label); };
      v->state_text = [this, is_male] { MessageBuilder m; m.fragment(male_ == is_male ? strings::kSelected : strings::kNotSelected); return m.build(); };
      b.add_item(ControlId::structural(std::string("create.sex.") + sex), v);
    }
    b.pop_context();
    {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kToggleType;
      v->announcements = {NodeAnnouncement([] { return std::string("Hardcore"); }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([this] { return std::string(hardcore_ ? strings::kOn : strings::kOff); }, true, announcement_kinds::kValue)};
      v->on_activate = [this] { hardcore_ = !hardcore_; click_label(kScreen, "Hardcore"); };
      v->state_text = [this] { return std::string(hardcore_ ? strings::kOn : strings::kOff); };
      b.add_item(ControlId::structural("create.hardcore"), v);
    }
    b.begin_stop("buttons");
    b.start_row("buttons");
    {
      // Next is greyed out by the game until a name is entered; we know the name, so we know the state.
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([] { return std::string("Next"); }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([this] { return name_.empty() ? std::string(strings::kDisabled) : std::string(); }, true, announcement_kinds::kEnabled)};
      v->on_activate = [this] {
        if (name_.empty()) { speech::speak(strings::kDisabled, true); return; }
        submitted_ = true;
        click_label(kScreen, "Next");
      };
      b.add_item(ControlId::structural("create.Next"), v);
    }
    b.add_item(ControlId::structural("create.Cancel"), click_button(kScreen, "Cancel"));
    b.end_row();
  }

 private:
  void start_editing() {
    editing_ = true;
    click_label(kScreen, "Name", 0, 27);  // the field's box is just under its label; the click gives it the game's focus
    speech::speak(strings::kEditing, true);
  }
  void stop_editing() {
    if (!editing_) return;
    editing_ = false;
    // Checkpoint: the game greys Next out while its field is empty. If we believe there is a name and the
    // game does not, say so instead of letting Next "clonk" uselessly (and log it for the dev loop).
    textcap::Item next;
    if (!name_.empty() && textcap::find_item("Next", next) && next.rgba == 0x80808080) {
      log::writef("create character: name '{}' in our state but the game's Next is still disabled", name_);
      name_.clear();
      speech::speak(strings::kNameNotTaken, true);
    }
    if (GraphNavigator* nav = app::navigator()) nav->announce_current();
  }

  bool male_ = true;
  bool hardcore_ = false;
  std::string name_;
  bool editing_ = false;
  bool submitted_ = false;  // Next was activated: the dialog may come back (Difficulty Select's Back) with our values intact
};

std::unique_ptr<Screen> make_create_character() { return std::make_unique<CreateCharacterScreen>(); }
}  // namespace gd::screens
