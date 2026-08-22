#include "screens/tip.h"
#include <algorithm>
#include <cstdlib>
#include <format>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "tip";
static const ControlType kLineType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};
// The popup sits top-left (measured 2026-08-21 at 1600x900: lines at x 197, y 73..121); its lines are drawn
// one per RenderText2d call. The title is the first line; the body is whatever sits under it in that column.
constexpr int kTitleMaxX = 600, kTitleMaxY = 250, kColumnSlack = 260, kBodyHeight = 220;

// The on-screen line that starts a recently fetched tip: a drawn line that is a prefix of the tip text
// (the loc string's first line is its title).
static bool find_title(textcap::Item& title) {
  std::vector<hooks::Tip> tips = hooks::recent_tips();
  for (const textcap::Item& it : textcap::snapshot()) {
    if (it.x > kTitleMaxX || it.y > kTitleMaxY) continue;
    std::string t = textcap::speakable(it.text);
    if (t.size() < 4) continue;
    for (const hooks::Tip& tip : tips)
      if (tip.text.rfind(t, 0) == 0) { title = it; return true; }
  }
  return false;
}
static std::vector<std::string> body_lines(const textcap::Item& title) {
  std::vector<std::pair<int, std::string>> lines;
  for (const textcap::Item& it : textcap::snapshot()) {
    if (it.y <= title.y || it.y > title.y + kBodyHeight || std::abs(it.x - title.x) > kColumnSlack) continue;
    std::string t = textcap::speakable(it.text);
    if (!t.empty()) lines.push_back({it.y, t});
  }
  std::sort(lines.begin(), lines.end());
  std::vector<std::string> out;
  for (auto& [y, t] : lines)
    if (out.empty() || out.back() != t) out.push_back(t);  // the game draws each line twice (shadow pass)
  return out;
}

class TipScreen : public Screen {
 public:
  std::string_view key() const override { return "tip"; }
  bool is_active() override { textcap::Item t; return find_title(t); }
  std::string screen_name() const override { return std::string(strings::kTip); }
  int layer() const override { return 35; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { close(); }}}; }
  void on_focus() override {
    Screen::on_focus();
    // Read the whole tip at once on arrival; the items below let the player re-read line by line.
    textcap::Item title;
    if (!find_title(title)) return;
    MessageBuilder m;
    m.fragment(textcap::speakable(title.text));
    for (const std::string& l : body_lines(title)) m.fragment(l);
    speech::speak(m.build(), false);
  }
  void build(GraphBuilder& b) override {
    textcap::Item title;
    if (!find_title(title)) return;
    b.begin_stop("tip");
    std::vector<std::string> lines = body_lines(title);
    lines.insert(lines.begin(), textcap::speakable(title.text));
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
  // The game closes a tip on a right click over it.
  static void close() {
    textcap::Item title;
    if (find_title(title)) hooks::click((float)title.x, (float)(title.y + 10), 2);
  }
};

std::unique_ptr<Screen> make_tip() { return std::make_unique<TipScreen>(); }
}  // namespace gd::screens
