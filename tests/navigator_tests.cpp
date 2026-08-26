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

// Regression (2026-08-26): the search scope used to hold GraphNode* from the render you typed in; every arrow
// through the results rerenders (focus_at_column), so by the second arrow those pointers were freed arena memory
// and the focus landed on whatever the stale id resolved to (the tab row included). The scope is by ControlId now.
namespace {
struct WordsScreen : Screen {
  int builds = 0;
  std::string_view key() const override { return "words"; }
  bool is_active() override { return true; }
  std::string screen_name() const override { return "words"; }
  void build(GraphBuilder& b) override {
    ++builds;
    b.begin_stop("items");
    b.add_item(id("apple"), vt("apple"));
    b.add_item(id("banana"), vt("banana"));
    b.add_item(id("avocado"), vt("avocado"));
    b.add_item(id("apricot"), vt("apricot"));
  }
};
}  // namespace

TEST_CASE("type-ahead results survive rerenders between the typing and the arrows") {
  std::vector<std::string> spoken;
  int frame = 0;
  WordsScreen screen;
  GraphNavigator nav{NavigatorHost{[&](std::string_view t, bool) { spoken.emplace_back(t); }, {}, [] { return true; }, [&] { return frame; }}};
  nav.attach(&screen); nav.ensure_focus();
  auto tick = [&](std::u16string_view typed, bool down) {
    TypeaheadInput in; in.typed = typed; in.down_held = down; in.dt = 0.016; ++frame;
    spoken.clear(); nav.tick_typeahead(in);
    return spoken.empty() ? std::string() : spoken.back();
  };
  tick(u"", false);                        // the first tick only registers the screen for type-ahead
  CHECK(tick(u"a", false) == "apple");
  // Many rerenders before the first arrow: the render the scope was built from is long retired.
  for (int i = 0; i < 5; ++i) nav.announce_current();
  CHECK(tick(u"", true) == "avocado");
  CHECK(tick(u"", false) == "");           // arrow released: no step
  for (int i = 0; i < 5; ++i) nav.announce_current();
  CHECK(tick(u"", true) == "apricot");
  CHECK(tick(u"", false) == "");
  CHECK(tick(u"", true) == "banana");      // the substring tier (the a in banana) ranks last
  CHECK(tick(u"", false) == "");
  CHECK(tick(u"", true) == "apple");       // and the results wrap
  CHECK(screen.builds > 10);
}
