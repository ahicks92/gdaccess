#include "screens/reward_list.h"
#include <format>
#include "core/graph_builder.h"
#include "core/strings.h"
#include "screens/window_base.h"   // line_item / row_item

namespace gd::screens {
using namespace gd::core;

void add_reward_lines(GraphBuilder& b, const std::string& prefix, const std::vector<std::string>& header,
                      const std::vector<std::string>& rewards) {
  int i = 0;
  for (const std::string& s : header)
    if (!s.empty()) b.add_item(ControlId::structural(std::format("{}.line{}", prefix, i++)), line_item(s));
  i = 0;
  for (const std::string& r : rewards) b.add_item(ControlId::structural(std::format("{}.row{}", prefix, i++)), line_item(r));
}

namespace {
struct Ctx {
  bool open = false;
  std::string title;
  std::vector<std::string> rewards;
};
Ctx g_ctx;
}  // namespace

void open_reward_notice(std::string title, std::vector<std::string> rewards) {
  g_ctx.title = std::move(title);
  g_ctx.rewards = std::move(rewards);
  g_ctx.open = true;
}

// A mod-owned overlay, not tied to any game window: sits above the launching screen (high layer); when it closes
// the launcher becomes current again and refocuses the row it was opened from.
class RewardNoticeScreen : public Screen {
 public:
  std::string_view key() const override { return "reward_notice"; }
  bool is_active() override { return g_ctx.open; }
  std::string screen_name() const override { return g_ctx.title; }
  int layer() const override { return 30; }   // with the list picker / count prompt, above the service windows
  bool exclusive() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { g_ctx.open = false; }}};
  }
  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    add_reward_lines(b, "notice", {}, g_ctx.rewards);
    b.add_item(ControlId::structural("notice.close"), row_item(std::string(strings::kClose), {}, [] { g_ctx.open = false; }));
  }
};

std::unique_ptr<Screen> make_reward_notice() { return std::make_unique<RewardNoticeScreen>(); }

}  // namespace gd::screens
