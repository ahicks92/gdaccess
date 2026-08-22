#include "screens/skills.h"
#include <algorithm>
#include <format>
#include "app.h"
#include "core/navigator.h"
#include "gameapi.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

// The skill window (N). Tabs = the character's mastery slots (one per allowed mastery): a slot without a class
// shows the class-selection list (six masteries; Space reads the description, Enter puts that class's tree on
// the game's tab -- undoable until the mastery takes its first point); a slot with a class shows the skill-points
// line, the mastery bar, then the class's skills in tier order. Enter spends a point, Backspace refunds one,
// Space reads the game's skill text. Ctrl+1..0 / Ctrl+J / Ctrl+I assign the focused skill to the quickbar.
class SkillsScreen : public WindowScreen, public AssignSource {
 public:
  SkillsScreen() : WindowScreen("skills", std::string(strings::kSkills), exe_ui::ingame::kSkills, 12) {}
  void on_focus() override { refresh(); WindowScreen::on_focus(); }
  void on_tab_changed(int) override { refresh(); }

  void build(GraphBuilder& b) override {
    const std::vector<gameapi::SkillInfo>& list = skills_.get([] { return gameapi::skills(); }, 30);
    std::vector<unsigned> have = gameapi::mastery_ids();   // enumerations of the committed masteries
    unsigned allowed = gameapi::masteries_allowed();
    int slots = (int)allowed > (int)have.size() ? (int)allowed : (int)have.size();
    if (slots < 1) slots = 1;
    if (slots > 2) slots = 2;
    std::vector<std::string> labels;
    for (int t = 0; t < slots; ++t) {
      int e = t < (int)have.size() ? (int)have[(size_t)t] : chosen_[t];
      std::string name = e >= 0 ? mastery_name(e) : std::string(strings::kSelectClass);
      labels.push_back(name.empty() ? std::string(strings::kSelectClass) : name);
    }
    add_tabs(b, labels);
    int t = tab();
    bool committed = t < (int)have.size();
    int e = committed ? (int)have[(size_t)t] : chosen_[t];
    if (e < 0) { build_select(b, t); return; }
    build_tree(b, list, e, committed);
  }

  unsigned focused_skill_id() override {
    GraphNavigator* nav = app::navigator();
    std::optional<ControlId> id = nav ? nav->focused_id() : std::nullopt;
    if (!id || !id->structural_key().is_string()) return 0;
    const std::string& k = id->structural_key().text();
    if (k.rfind("skills.s", 0) != 0) return 0;
    return (unsigned)strtoul(k.c_str() + 8, nullptr, 10);
  }
  std::string focused_label() override {
    unsigned id = focused_skill_id();
    for (const gameapi::SkillInfo& s : skills_.value) if (s.id == id) return s.name;
    return {};
  }

 private:
  void refresh() { skills_.invalidate(); choices_.invalidate(); }
  std::string mastery_name(int e) { for (const gameapi::MasteryChoice& c : choices_.get([] { return gameapi::mastery_choices(); }, 600)) if (c.enumeration == e) return c.name; return {}; }

  void build_select(GraphBuilder& b, int t) {
    const std::vector<gameapi::MasteryChoice>& cs = choices_.get([] { return gameapi::mastery_choices(); }, 600);
    if (cs.empty()) { b.add_item(ControlId::structural("skills.nochoice"), line_item(std::string(strings::kEmpty))); return; }
    for (const gameapi::MasteryChoice& c : cs) {
      int e = c.enumeration; std::string desc = c.description;
      auto choose = [this, t, e] {
        if (!exe_ui::skills_set_pane(t, e)) { speech::speak(strings::kCannot, true); return; }
        chosen_[t] = e;
        refresh();
        speech::speak(strings::kClassChosen, true);
      };
      auto describe = [desc] { speech::speak(desc.empty() ? std::string(strings::kNoTooltip) : desc, true); };
      b.add_item(ControlId::structural(std::format("skills.choice{}", e)), row_item(c.name, {}, choose, describe));
    }
  }

  void build_tree(GraphBuilder& b, const std::vector<gameapi::SkillInfo>& list, int e, bool committed) {
    { MessageBuilder m; strings::push_stat(m, strings::kSkillPointsLeft, std::format("{}", gameapi::skill_points())); b.add_item(ControlId::structural("skills.points"), line_item(m.build())); }
    const gameapi::SkillInfo* mastery = gameapi::mastery_skill(list, e);
    if (mastery) add_skill(b, *mastery);
    if (!committed && mastery && mastery->level == 0) {
      int t = tab();
      auto undo = [this, t] { if (exe_ui::skills_set_pane(t, exe_ui::kSkillsClassSelectPane)) { chosen_[t] = -1; refresh(); } };
      b.add_item(ControlId::structural("skills.undo"), row_item(std::string(strings::kUndoClassSelection), {}, undo));
    }
    unsigned mid = mastery ? mastery->id : 0;
    std::vector<const gameapi::SkillInfo*> tree;
    for (const gameapi::SkillInfo& s : list) if (!s.is_mastery && mid && s.mastery_id == mid) tree.push_back(&s);
    std::stable_sort(tree.begin(), tree.end(), [](const gameapi::SkillInfo* a, const gameapi::SkillInfo* c) { return a->mastery_req != c->mastery_req ? a->mastery_req < c->mastery_req : a->tier < c->tier; });
    for (const gameapi::SkillInfo* s : tree) add_skill(b, *s);
    if (!mastery) b.add_item(ControlId::structural("skills.nomastery"), line_item(std::string(strings::kEmpty)));
  }

  void add_skill(GraphBuilder& b, const gameapi::SkillInfo& s) {
    std::string label = s.name.empty() ? s.record : s.name;
    unsigned level = s.level, max = s.max_level, req = s.mastery_req; bool locked = s.locked, mastery = s.is_mastery, modifier = s.modifier;
    auto value = [level, max, req, locked, mastery, modifier] {
      MessageBuilder m;
      strings::push_skill_level(m, level, max);
      if (mastery) m.list_item().fragment(strings::kMastery);
      if (modifier) m.list_item().fragment(strings::kModifier);
      if (req) m.list_item().fragment(strings::kRequiresMastery).fragment(std::format("{}", req));
      if (locked) m.list_item().fragment(strings::kLocked);
      return m.build();
    };
    void* p = s.p; unsigned id = s.id;
    auto learn = [this, p, id] {
      void* q = gameapi::object_by_id(id); if (!q) q = p;
      if (gameapi::skill_points() == 0) { speech::speak(strings::kNoPoints, true); return; }
      speech::speak(gameapi::learn_skill(q) ? std::string(strings::kPointSpent) : std::string(strings::kCannot), true);
      refresh();
    };
    auto refund = [this, p, id] { void* q = gameapi::object_by_id(id); if (!q) q = p; if (!gameapi::refund_skill(q)) speech::speak(strings::kCannot, true); refresh(); };
    auto tooltip = [p] { speak_lines(gameapi::skill_tooltip(p)); };
    auto v = row_item(label, value, learn, tooltip, refund);
    v->state_text = [id] { for (const gameapi::SkillInfo& x : gameapi::skills()) if (x.id == id) { MessageBuilder m; strings::push_skill_level(m, x.level, x.max_level); return m.build(); } return std::string(); };
    b.add_item(ControlId::structural(std::format("skills.s{}", s.id)), v);
  }

  Snapshot<std::vector<gameapi::SkillInfo>> skills_;
  Snapshot<std::vector<gameapi::MasteryChoice>> choices_;
  int chosen_[2] = {-1, -1};   // the class put on each tab this session but not yet committed (ours; the game's pane follows)
};

std::unique_ptr<Screen> make_skills() { return std::make_unique<SkillsScreen>(); }
}  // namespace gd::screens
