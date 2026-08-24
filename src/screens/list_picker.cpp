#include "screens/list_picker.h"
#include <string>
#include "core/graph_builder.h"
#include "core/strings.h"
#include "screens/window_base.h"   // row_item / line_item
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

namespace {
struct Ctx {
  bool open = false;
  std::string title;
  std::vector<PickerItem> items;
  std::function<void(unsigned)> on_pick;
  std::function<void(unsigned, bool)> tooltip;
};
Ctx g_ctx;
}  // namespace

void open_picker(std::string title, std::vector<PickerItem> items, std::function<void(unsigned)> on_pick,
                 std::function<void(unsigned, bool)> tooltip) {
  g_ctx.title = std::move(title);
  g_ctx.items = std::move(items);
  g_ctx.on_pick = std::move(on_pick);
  g_ctx.tooltip = std::move(tooltip);
  g_ctx.open = true;
}
bool picker_open() { return g_ctx.open; }

// A mod-owned overlay: sits above the launching screen (high layer); when it closes the launcher becomes
// current again and refocuses the slot it was opened from. Not tied to any game window.
class ListPickerScreen : public Screen {
 public:
  std::string_view key() const override { return "list_picker"; }
  bool is_active() override { return g_ctx.open; }
  std::string screen_name() const override { return g_ctx.title; }
  int layer() const override { return 30; }   // above the service windows (11/14) and the hotbar manager (12)
  bool exclusive() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  bool allows_typeahead() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { g_ctx.open = false; }}};
  }
  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    if (g_ctx.items.empty()) { b.add_item(ControlId::structural("picker.empty"), line_item(std::string(strings::kEmpty))); return; }
    for (size_t i = 0; i < g_ctx.items.size(); ++i) {
      const PickerItem& it = g_ctx.items[i];
      unsigned id = it.id;
      std::string value = it.value;
      std::function<void()> tip, tip_detail;
      if (g_ctx.tooltip && id) { tip = [id] { if (g_ctx.tooltip) g_ctx.tooltip(id, false); }; tip_detail = [id] { if (g_ctx.tooltip) g_ctx.tooltip(id, true); }; }
      b.add_item(ControlId::structural("picker." + std::to_string(i)),
                 row_item(it.label, value.empty() ? std::function<std::string()>{} : [value] { return value; }, [id] { pick(id); }, tip, {}, tip_detail));
    }
  }

 private:
  static void pick(unsigned id) {
    std::function<void(unsigned)> fn = g_ctx.on_pick;   // copy: on_pick may outlive the close
    g_ctx.open = false;
    if (fn) fn(id);
  }
};

std::unique_ptr<Screen> make_list_picker() { return std::make_unique<ListPickerScreen>(); }

}  // namespace gd::screens
