#include "screens/count_prompt.h"
#include <format>
#include "app.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "hooks.h"
#include "log.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

namespace {
struct Ctx {
  bool open = false;
  std::string title;
  unsigned max = 0;
  std::string digits;
  std::function<void(unsigned)> on_commit;
  uint64_t opened_frame = 0;
};
Ctx g_ctx;
namespace keys { constexpr int Escape = 0x01, Enter = 0x1c, Backspace = 0x0e; }
}  // namespace

void open_count_prompt(std::string title, unsigned max, std::function<void(unsigned)> on_commit) {
  g_ctx.title = std::move(title);
  g_ctx.max = max;
  g_ctx.digits.clear();
  g_ctx.on_commit = std::move(on_commit);
  g_ctx.opened_frame = hooks::frame();
  g_ctx.open = true;
}
bool count_prompt_open() { return g_ctx.open; }

// Raw-input screen: no graph, the keys are read here (app.cpp stands the navigator down while
// captures_raw_input() is true, so the Enter that commits never re-activates the launching row).
class CountPromptScreen : public Screen {
 public:
  std::string_view key() const override { return "count_prompt"; }
  bool is_active() override { return g_ctx.open; }
  std::string screen_name() const override { return g_ctx.title; }
  int layer() const override { return 30; }
  bool exclusive() const override { return true; }
  bool captures_raw_input() const override { return true; }
  bool allows_typeahead() const override { return false; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }

  void on_update() override {
    if (!g_ctx.open) return;
    // The chord that opened the prompt is still "just pressed" on the frame it becomes current.
    if (hooks::frame() == g_ctx.opened_frame) return;
    const KeySource& ks = hooks::key_source();
    // The digit row by scancode (0x02 = '1' .. 0x0a = '9', 0x0b = '0'); the game's per-event character is not
    // consulted (a synthetic key has none, and letters/symbols are not wanted anyway).
    for (int code = 0x02; code <= 0x0b; ++code) {
      if (!ks.just_pressed(code) || ks.ctrl() || ks.alt()) continue;
      if (g_ctx.digits.size() >= 9) continue;
      char c = code == 0x0b ? '0' : (char)('0' + code - 1);
      g_ctx.digits.push_back(c);
      speech::speak(std::string(1, c), true);
    }
    if (ks.just_pressed(keys::Backspace)) {
      if (!g_ctx.digits.empty()) g_ctx.digits.pop_back();
      speech::speak(g_ctx.digits.empty() ? std::string(strings::kEmpty) : g_ctx.digits, true);
    }
    if (ks.just_pressed(keys::Escape)) { g_ctx.open = false; return; }
    if (ks.just_pressed(keys::Enter)) commit();
  }

 private:
  static void commit() {
    unsigned n = g_ctx.digits.empty() ? 0u : (unsigned)strtoul(g_ctx.digits.c_str(), nullptr, 10);
    if (n < 1 || n > g_ctx.max) {
      MessageBuilder m; strings::push_range_hint(m, 1, g_ctx.max);
      speech::speak(m.build(), true);
      g_ctx.digits.clear();
      return;
    }
    std::function<void(unsigned)> fn = g_ctx.on_commit;   // copy: on_commit may outlive the close
    g_ctx.open = false;
    if (fn) fn(n);
  }
};

std::unique_ptr<Screen> make_count_prompt() { return std::make_unique<CountPromptScreen>(); }

}  // namespace gd::screens
