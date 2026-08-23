// GraphNavigator through a real Screen: edges of a list repeat the focused item instead of going silent.
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/navigator.h"
#include "core/screen.h"

using namespace gd::core;

namespace {
ControlId id(const char* s) { return ControlId::structural(s); }
std::shared_ptr<NodeVtable> vt(std::string label) {
  auto v = std::make_shared<NodeVtable>();
  v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel)};
  return v;
}

struct ListScreen : Screen {
  bool row = false;
  std::string_view key() const override { return "list"; }
  bool is_active() override { return true; }
  std::string screen_name() const override { return "list"; }
  void build(GraphBuilder& b) override {
    b.begin_stop("items");
    if (row) b.start_row("items");
    b.add_item(id("a"), vt("a"));
    b.add_item(id("b"), vt("b"));
    b.add_item(id("c"), vt("c"));
    if (row) b.end_row();
  }
};

struct Harness {
  std::vector<std::string> spoken;
  int frame = 0;
  ListScreen screen;
  GraphNavigator nav{NavigatorHost{[this](std::string_view t, bool) { spoken.emplace_back(t); }, {}, [] { return true; }, [this] { return frame; }}};
  Harness() { nav.attach(&screen); nav.ensure_focus(); }
  std::string act(std::string_view key) { spoken.clear(); nav.on_action(key); return spoken.empty() ? std::string() : spoken.back(); }
};
}  // namespace

TEST_CASE("vertical list: Up at the top and Down at the bottom repeat the item; Left/Right say nothing") {
  Harness h;
  CHECK(h.spoken.back().find("a") != std::string::npos);
  CHECK(h.act(ui_actions::Up) == "a");        // already at the top: repeated
  CHECK(h.act(ui_actions::Down) == "b");
  CHECK(h.act(ui_actions::Down) == "c");
  CHECK(h.act(ui_actions::Down) == "c");      // bottom: repeated
  CHECK(h.act(ui_actions::Left) == "");       // not this list's axis
  CHECK(h.act(ui_actions::Right) == "");
  CHECK(h.act(ui_actions::End) == "c");       // Home/End at the edge repeat too
  CHECK(h.act(ui_actions::Home) == "a");
  CHECK(h.act(ui_actions::Home) == "a");
}

TEST_CASE("row: Left/Right repeat at the ends, Up/Down say nothing") {
  Harness h;
  h.screen.row = true;
  h.nav.attach(&h.screen);   // content changed: rebuild as a row
  h.frame += 100; h.nav.ensure_focus();
  CHECK(h.act(ui_actions::Left) == "a");
  CHECK(h.act(ui_actions::Right) == "b");
  CHECK(h.act(ui_actions::Right) == "c");
  CHECK(h.act(ui_actions::Right) == "c");
  CHECK(h.act(ui_actions::Up) == "");
  CHECK(h.act(ui_actions::Down) == "");
}
