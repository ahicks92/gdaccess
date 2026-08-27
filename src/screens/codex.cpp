#include "screens/codex.h"
#include <format>
#include "gameapi.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

// The codex (Q): three tabs over the exported quest repository and the lore codex. Quests are tree groups
// (Right expands): the quest row reads "name, tracked"; Enter toggles tracking; inside are the tasks with
// their description, objectives (done / open) and rewards. Lore notes are rows; Enter or Space reads the note.
class CodexScreen : public WindowScreen {
 public:
  CodexScreen() : WindowScreen("codex", std::string(strings::kCodex), exe_ui::ingame::kQuest, 12) {}
  bool wrap() const override { return false; }
  void on_tab_changed(int) override { quests_.invalidate(); }

  void build(GraphBuilder& b) override {
    add_tabs(b, {std::string(strings::kQuests), std::string(strings::kCompletedQuests), std::string(strings::kLore)});
    if (tab() == 2) { build_lore(b); return; }
    int filter = tab() == 0 ? gameapi::kQuestsInProgress : gameapi::kQuestsCompleted;
    const std::vector<gameapi::Quest>& qs = quests_.get([filter] { return gameapi::quests(filter); }, 30);
    if (qs.empty()) { b.add_item(ControlId::structural("codex.none"), line_item(std::string(strings::kNoQuests))); return; }
    std::string last_group;
    for (const gameapi::Quest& q : qs) {
      if (q.group != last_group) { last_group = q.group; }
      std::string id = std::format("codex.q{}", q.id);
      void* qp = q.p;
      bool tracked = q.tracked;
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kGroupType;
      std::string label = q.group.empty() ? q.name : q.name + ", " + q.group;
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([tracked] { return std::string(tracked ? strings::kTracked : strings::kNotTracked); }, true, announcement_kinds::kValue)};
      v->on_activate = [this, qp, tracked] { gameapi::set_quest_tracked(qp, !tracked); quests_.invalidate(); };
      v->state_text = [qp] { for (const gameapi::Quest& x : gameapi::quests(gameapi::kQuestsAll)) if (x.p == qp) return std::string(x.tracked ? strings::kTracked : strings::kNotTracked); return std::string(); };
      b.begin_group(ControlId::structural(id), v);
      int ti = 0;
      for (const gameapi::Task& t : q.tasks) {
        std::string tid = std::format("{}.t{}", id, ti++);
        MessageBuilder m;
        m.fragment(t.name.empty() ? t.description : t.name);
        if (!t.name.empty() && !t.description.empty()) m.list_item().fragment(t.description);
        if (t.state == 3) m.list_item().fragment(strings::kDone);
        b.add_item(ControlId::structural(tid), line_item(m.build()));
        int oi = 0;
        for (const gameapi::Objective& o : t.objectives) {
          MessageBuilder om;
          om.fragment(o.text);
          if (o.done()) om.list_item().fragment(strings::kDone);
          b.add_item(ControlId::structural(std::format("{}.o{}", tid, oi++)), line_item(om.build()));
        }
        int ri = 0;
        for (const std::string& r : t.rewards) {
          MessageBuilder rm; rm.fragment(strings::kReward).list_item().fragment(r);
          b.add_item(ControlId::structural(std::format("{}.r{}", tid, ri++)), line_item(rm.build()));
        }
      }
      b.end_group();
    }
  }

 private:
  void build_lore(GraphBuilder& b) {
    const std::vector<gameapi::Note>& notes = notes_.get([] { return gameapi::lore_notes(); }, 60);
    if (notes.empty()) { b.add_item(ControlId::structural("codex.nolore"), line_item(std::string(strings::kEmpty))); return; }
    for (const gameapi::Note& n : notes) {
      std::string label = n.title;
      if (label.empty()) {   // some notes' codex title tag localizes empty: the note item's own name (its tooltip's first line)
        std::vector<std::string> t = gameapi::note_text(gameapi::object_by_id(n.id));
        label = t.empty() ? std::format("note {}", n.id) : t.front();
      }
      std::string heading = n.heading;
      unsigned id = n.id;
      auto read = [id] {
        void* p = gameapi::object_by_id(id);
        std::vector<std::string> full = gameapi::note_full_text(p);   // the game's tooltip truncates long notes
        speak_lines(full.empty() ? gameapi::note_text(p) : full);
      };
      b.add_item(ControlId::structural(std::format("codex.n{}", n.id)), row_item(label, heading.empty() ? std::function<std::string()>{} : [heading] { return heading; }, read, read));
    }
  }
  Snapshot<std::vector<gameapi::Quest>> quests_;
  Snapshot<std::vector<gameapi::Note>> notes_;
};

std::unique_ptr<Screen> make_codex() { return std::make_unique<CodexScreen>(); }

// The current objectives. Prefer GameEngine::GetObjectives (the game's scripted/HUD list) when it is
// populated. It comes back empty in a lot of the campaign (verified live: a character with several tracked
// bounties still reads empty), so fall back to walking the tracked, incomplete quests. The old walk was the
// bug the user hit: it read EVERY task of every tracked quest, so a quest whose "slay" step is still open ALSO
// surfaced its "return to the bounty table" turn-in task -- the "return to xyz" noise. Read only each tracked
// incomplete quest's CURRENT step: the first not-completed task that still has an unsatisfied objective, and
// that task's open objectives (a later turn-in is not the current step until the steps before it are done).
void speak_objectives() {
  MessageBuilder m;
  std::vector<std::string> hud = gameapi::objectives();
  if (!hud.empty()) {
    for (const std::string& l : hud) m.list_item().fragment(l);
  } else {
    for (const gameapi::Quest& q : gameapi::quests(gameapi::kQuestsTracked)) {
      if (q.complete) continue;
      for (const gameapi::Task& t : q.tasks) {
        if (t.state == 3) continue;   // a completed task is not the current step
        bool any = false;
        for (const gameapi::Objective& o : t.objectives)
          if (!o.done()) { strings::push_quest_objective(m.list_item(), q.name, o.text); any = true; }
        if (any) break;               // only the current step of this quest
      }
    }
  }
  if (m.empty()) { speech::speak(strings::kNoObjectives, true); return; }
  speech::speak(m.build(), true);
}
}  // namespace gd::screens
