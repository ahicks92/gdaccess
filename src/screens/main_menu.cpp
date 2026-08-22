#include "screens/main_menu.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "main menu";

class MainMenuScreen : public Screen {
 public:
  std::string_view key() const override { return "main_menu"; }
  // Checkpoint: the menu's bottom-right version stamp is only drawn on the main menu, and no dialog we
  // know of is open. (Dialogs get their own screens at higher layers.)
  bool is_active() override { return textcap::has_text("Credits") && textcap::has_text("Create"); }
  std::string screen_name() const override { return "Main menu"; }
  int layer() const override { return 0; }
  void build(GraphBuilder& b) override {
    b.begin_stop("menu");
    for (const char* item : {"Create", "Delete", "Multiplayer", "Game Guide", "Community", "DLC", "Credits"})
      b.add_item(ControlId::structural(std::string("main_menu.") + item), click_button(kScreen, item));
    b.begin_stop("play");
    b.start_row("play");
    for (const char* item : {"Normal Difficulty", "Main Campaign", "Start"})
      b.add_item(ControlId::structural(std::string("main_menu.") + item), click_button(kScreen, item));
    b.end_row();
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
  void on_update() override { if (frames_ >= 0 && ++frames_ == kDebounceFrames) { speech::speak(strings::kUnsupportedScreen, false); frames_ = -1; } }
  int layer() const override { return -1; }
  // Not modelled: the game keeps its keyboard, we only say where we are.
  bool owns_keyboard() const override { return false; }
  std::vector<InputCategory> input_categories() const override { return {}; }

 private:
  static constexpr int kDebounceFrames = 30;
  int frames_ = 0;
};

std::unique_ptr<Screen> make_main_menu() { return std::make_unique<MainMenuScreen>(); }
std::unique_ptr<Screen> make_unsupported() { return std::make_unique<UnsupportedScreen>(); }
}  // namespace gd::screens
