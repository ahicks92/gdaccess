#include "screens/main_menu.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "core/message_builder.h"
#include "gameapi.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::MainMenu;

// The main menu, read from the exe's menu-manager object: its buttons are named slots on the manager (the
// captions are the game's own), and it is "showing" when the app is in a main-menu state with no sub-window
// (Create Character, Difficulty ...) and no popup open -- those get their own screens on top.
inline const ControlType kCharacterType{"character", {"label", "value", "selected", "position"}, [] { return std::vector<NodeAnnouncement>{}; }};

class MainMenuScreen : public Screen {
 public:
  std::string_view key() const override { return "main_menu"; }
  bool is_active() override {
    if (!exe_ui::available()) return false;
    int s = exe_ui::app_state();
    if (s != 3 && s != 4 && s != 8) return false;
    MainMenu mm = exe_ui::main_menu();
    return mm && !mm.current_sub_window() && !exe_ui::popup();
  }
  std::string screen_name() const override { return "Main menu"; }
  int layer() const override { return 0; }
  // Three Tab stops (decided with the user 2026-08-22): the general buttons; the character list, only when
  // there is more than one character (one character is simply the selected one); the character-specific
  // buttons (Start, difficulty, game mode, Delete).
  void build(GraphBuilder& b) override {
    MainMenu mm = exe_ui::main_menu();
    if (!mm) return;
    struct Slot { const char* id; unsigned off; std::string_view label; };
    b.begin_stop("menu");
    for (Slot s : {Slot{"Create", MainMenu::kBtnCreate, {}}, Slot{"Multiplayer", MainMenu::kBtnMultiplayer, {}},
                   Slot{"GameGuide", MainMenu::kBtnGameGuide, {}}, Slot{"Community", MainMenu::kBtnCommunity, {}}, Slot{"DLC", MainMenu::kBtnDLC, {}},
                   Slot{"Credits", MainMenu::kBtnCredits, {}}, Slot{"Options", MainMenu::kBtnOptions, strings::kOptions}, Slot{"Exit", MainMenu::kBtnExit, strings::kExitGame}})
      add(b, mm, s.id, s.off, s.label);
    std::vector<MainMenu::Character> chars = mm.characters();
    if (chars.size() > 1) {
      int selected = mm.selected_character();
      b.begin_stop("characters");
      for (size_t i = 0; i < chars.size(); ++i) {
        const MainMenu::Character& c = chars[i];
        std::string cls = c.class_tag.empty() ? std::string() : gameapi::localize(c.class_tag);
        std::string value; { MessageBuilder m; strings::push_character_summary(m, c.level, cls, c.hardcore); value = m.build(); }
        bool sel = (int)i == selected;
        auto v = std::make_shared<NodeVtable>();
        v->control_type = &kCharacterType;
        v->announcements = {NodeAnnouncement([name = c.name] { return name; }, false, announcement_kinds::kLabel),
                            NodeAnnouncement([value] { return value; }, false, announcement_kinds::kValue),
                            NodeAnnouncement([sel] { return sel ? std::string(strings::kSelected) : std::string(); }, true, announcement_kinds::kSelected)};
        v->on_activate = [i] { MainMenu m = exe_ui::main_menu(); if (m) m.select_character((int)i); };
        b.add_item(ControlId::structural("main_menu.character." + c.name), v);
      }
    }
    b.begin_stop("play");
    b.start_row("play");
    for (Slot s : {Slot{"Start", MainMenu::kBtnStart, {}}, Slot{"Difficulty", MainMenu::kBtnDifficulty, {}}, Slot{"GameMode", MainMenu::kBtnGameMode, {}}, Slot{"Delete", MainMenu::kBtnDelete, {}}})
      add(b, mm, s.id, s.off, s.label);
    b.end_row();
  }

 private:
  static void add(GraphBuilder& b, const MainMenu& mm, const char* id, unsigned off, std::string_view label) {
    exe_ui::WidgetA w = mm.button(off);
    if (!w.is_button() || !w.active()) return;  // a slot the game has not filled (multiplayer-only entries)
    b.add_item(ControlId::structural(std::string("main_menu.") + id), widget_button(w, std::string(label)));
  }
};

class UnsupportedScreen : public Screen {
 public:
  std::string_view key() const override { return "unsupported"; }
  bool is_active() override { return true; }
  // Announced only after it has been current for a moment: screen transitions (menu -> loading -> world)
  // pass through a few frames where nothing is modelled, and "unsupported screen" every time was noise.
  std::string screen_name() const override { return {}; }
  void on_focus() override { Screen::on_focus(); frames_ = 0; }
  void on_update() override {
    if (frames_ >= 0 && ++frames_ == kDebounceFrames) {
      frames_ = -1;
      // The exe layout check runs on first use; a mismatched game build is announced once, then the
      // fallback keeps the keyboard with the game.
      if (!exe_ui::available() && !version_warned_) { version_warned_ = true; speech::speak(strings::kUnsupportedGameVersion, false); }
      else speech::speak(strings::kUnsupportedScreen, false);
    }
  }
  int layer() const override { return -1; }
  // Not modelled: the game keeps its keyboard, we only say where we are.
  bool owns_keyboard() const override { return false; }
  std::vector<InputCategory> input_categories() const override { return {}; }

 private:
  static constexpr int kDebounceFrames = 30;
  int frames_ = 0;
  bool version_warned_ = false;
};

std::unique_ptr<Screen> make_main_menu() { return std::make_unique<MainMenuScreen>(); }
std::unique_ptr<Screen> make_unsupported() { return std::make_unique<UnsupportedScreen>(); }
}  // namespace gd::screens
