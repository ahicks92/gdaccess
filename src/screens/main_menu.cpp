#include "screens/main_menu.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::MainMenu;

// The main menu, read from the exe's menu-manager object: its buttons are named slots on the manager (the
// captions are the game's own), and it is "showing" when the app is in a main-menu state with no sub-window
// (Create Character, Difficulty ...) and no popup open -- those get their own screens on top.
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
  void build(GraphBuilder& b) override {
    MainMenu mm = exe_ui::main_menu();
    if (!mm) return;
    struct Slot { const char* id; unsigned off; std::string_view label; };
    b.begin_stop("menu");
    for (Slot s : {Slot{"Create", MainMenu::kBtnCreate, {}}, Slot{"Delete", MainMenu::kBtnDelete, {}}, Slot{"Multiplayer", MainMenu::kBtnMultiplayer, {}},
                   Slot{"GameGuide", MainMenu::kBtnGameGuide, {}}, Slot{"Community", MainMenu::kBtnCommunity, {}}, Slot{"DLC", MainMenu::kBtnDLC, {}},
                   Slot{"Credits", MainMenu::kBtnCredits, {}}, Slot{"Options", MainMenu::kBtnOptions, strings::kOptions}, Slot{"Exit", MainMenu::kBtnExit, strings::kExitGame}})
      add(b, mm, s.id, s.off, s.label);
    b.begin_stop("play");
    b.start_row("play");
    for (Slot s : {Slot{"Difficulty", MainMenu::kBtnDifficulty, {}}, Slot{"GameMode", MainMenu::kBtnGameMode, {}}, Slot{"Start", MainMenu::kBtnStart, {}}})
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
