#include "quest_rewards.h"
#include <windows.h>
#include <format>
#include <mutex>
#include "core/message_builder.h"
#include "core/strings.h"
#include "gameapi.h"
#include "gameapi_internal.h"
#include "gd_names.h"
#include "hooks.h"
#include "log.h"

namespace gd::quest_rewards {
namespace {
using namespace gd::names;
using namespace gd::gameapi::detail;
using gd::core::MessageBuilder;

// The reward model, all Game.dll exports (static RE 2026-09-06, docs/ingame-ui-survey.md "Quest reward").
struct Api {
  const void* (*Task_GetQuest)(const void*) = nullptr;
  const MsvcStringW* (*Task_GetName)(const void*) = nullptr;
  const MsvcStringW* (*Quest2_GetName)(const void*) = nullptr;
  bool (*Quest2_IsComplete)(const void*, bool) = nullptr;
  const MemVec* (*Collection_GetActions)(const void*) = nullptr;
  bool (*IsReward)(void*) = nullptr;
  unsigned (*Experience_GetAmount)(void*) = nullptr;
  int (*Money_GetAmount)(void*) = nullptr;
  unsigned (*SkillPoint_GetAmount)(void*) = nullptr;
  unsigned (*AttribPoint_GetAmount)(void*) = nullptr;
  int (*Devotion_GetAmount)(void*) = nullptr;
  unsigned (*Level_GetAmount)(void*) = nullptr;
  int (*Tribute_GetAmount)(void*) = nullptr;
  int (*Faction_GetAmount)(void*) = nullptr;
  MsvcStringA* (*Faction_GetFactionTag)(void*, MsvcStringA*) = nullptr;   // std::string by value: hidden pointer 2nd
  int (*FactionPack_GetFactionFromString)(const MsvcStringA*) = nullptr;
  MsvcStringA* (*FactionPack_GetFactionTag)(MsvcStringA*, int) = nullptr;  // static: hidden pointer FIRST
  unsigned (*Item_GetNumItems)(const void*) = nullptr;
  void* (*Item_GetInfoItem)(const void*, unsigned) = nullptr;
  int (*Item_GetCount)(const void*) = nullptr;
  // The concrete classes' vtables (the exported `vftable' symbol is the object's vptr value). GiveFaction's is
  // not exported: a reward action matching none of these is the faction one.
  struct Vt { const void* p; Kind kind; } vts[9] = {};
  bool loaded = false;
};
Api g;

void load() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, Task_GetQuest, Quest2Task_GetQuest);
  GAPI_LOAD(g, Task_GetName, Quest2Task_GetName);
  GAPI_LOAD(g, Quest2_GetName, Quest2_GetName);
  GAPI_LOAD(g, Quest2_IsComplete, Quest2_IsComplete);
  GAPI_LOAD(g, Collection_GetActions, ScriptableActionCollection_GetActions);
  GAPI_LOAD(g, IsReward, ScriptableAction_IsReward);
  GAPI_LOAD(g, Experience_GetAmount, GiveExperience_GetAmount);
  GAPI_LOAD(g, Money_GetAmount, GiveMoney_GetAmount);
  GAPI_LOAD(g, SkillPoint_GetAmount, GiveSkillPoint_GetAmount);
  GAPI_LOAD(g, AttribPoint_GetAmount, GiveAttribPoint_GetAmount);
  GAPI_LOAD(g, Devotion_GetAmount, GiveDevotion_GetAmount);
  GAPI_LOAD(g, Level_GetAmount, GiveLevel_GetAmount);
  GAPI_LOAD(g, Tribute_GetAmount, GiveTribute_GetAmount);
  GAPI_LOAD(g, Faction_GetAmount, GiveFaction_GetAmount);
  GAPI_LOAD(g, Faction_GetFactionTag, GiveFaction_GetFactionTag);
  GAPI_LOAD(g, FactionPack_GetFactionFromString, FactionPack_GetFactionFromString);
  GAPI_LOAD(g, FactionPack_GetFactionTag, FactionPack_GetFactionTag);
  GAPI_LOAD(g, Item_GetNumItems, GiveItem_GetNumItems);
  GAPI_LOAD(g, Item_GetInfoItem, GiveItem_GetInfoItem);
  GAPI_LOAD(g, Item_GetCount, GiveItem_GetCount);
  struct { const char* dll; const char* name; Kind kind; } syms[9] = {
      {GiveExperience_vftable_DLL, GiveExperience_vftable, Kind::Experience}, {GiveMoney_vftable_DLL, GiveMoney_vftable, Kind::Money},
      {GiveItem_vftable_DLL, GiveItem_vftable, Kind::Item},                   {GiveRandomItem_vftable_DLL, GiveRandomItem_vftable, Kind::Item},
      {GiveSkillPoint_vftable_DLL, GiveSkillPoint_vftable, Kind::SkillPoints}, {GiveAttribPoint_vftable_DLL, GiveAttribPoint_vftable, Kind::AttributePoints},
      {GiveDevotion_vftable_DLL, GiveDevotion_vftable, Kind::DevotionPoints}, {GiveLevel_vftable_DLL, GiveLevel_vftable, Kind::Levels},
      {GiveTribute_vftable_DLL, GiveTribute_vftable, Kind::Tribute}};
  for (int i = 0; i < 9; ++i) {
    HMODULE m = GetModuleHandleA(syms[i].dll);
    g.vts[i] = {m ? (const void*)GetProcAddress(m, syms[i].name) : nullptr, syms[i].kind};
    if (!g.vts[i].p) log::writef("quest_rewards: vftable {} not found", syms[i].name);
  }
}

// ---- state (game thread) ----
std::vector<gd::hooks::Hook> g_hooks;
std::vector<std::vector<Reward>> g_pending;   // one collector per nested Quest2Task::Complete
std::mutex g_mu;
std::vector<Record> g_records;                // newest last
unsigned long long g_completes = 0, g_collections = 0;

const void* vptr_of(const void* obj) {
  const void* v = nullptr;
  guarded("reward vptr", [&] { v = *(const void* const*)obj; });
  return v;
}

Kind kind_of(void* action) {
  const void* vt = vptr_of(action);
  if (!vt) return Kind::Unknown;
  for (const Api::Vt& e : g.vts) if (e.p && e.p == vt) return e.kind;
  return Kind::Reputation;   // IsReward said yes and it is none of the exported classes: ScriptableAction_GiveFaction
}

// The faction's display name from the action's faction string (the record identifier GetFactionFromString
// takes), through the FactionPack tag and the game's localization -- the same path gameapi::factions() uses.
std::string faction_name(void* action) {
  std::string name;
  if (!g.Faction_GetFactionTag || !g.FactionPack_GetFactionFromString || !g.FactionPack_GetFactionTag) return name;
  std::string tag;
  guarded("reward faction", [&] {
    MsvcStringA s; init_a(s);
    g.Faction_GetFactionTag(action, &s);
    int type = g.FactionPack_GetFactionFromString(&s);
    std::string raw = take_a(s);
    if (type == -1000) { tag = raw; return; }   // FactionType(-1000) = unknown: keep the raw identifier
    MsvcStringA t; init_a(t);
    g.FactionPack_GetFactionTag(&t, type);
    tag = take_a(t);
  });
  if (tag.empty()) return name;
  name = gd::gameapi::localize(tag);
  return name.empty() ? tag : name;
}

// Read one reward action into rows (an item action can carry several items).
void read_action(void* a, std::vector<Reward>& out) {
  Kind k = kind_of(a);
  Reward r{k};
  switch (k) {
    case Kind::Experience: if (g.Experience_GetAmount) guarded("reward xp", [&] { r.amount = g.Experience_GetAmount(a); }); out.push_back(r); break;
    case Kind::Money: if (g.Money_GetAmount) guarded("reward bits", [&] { r.amount = g.Money_GetAmount(a); }); out.push_back(r); break;
    case Kind::SkillPoints: if (g.SkillPoint_GetAmount) guarded("reward sp", [&] { r.amount = g.SkillPoint_GetAmount(a); }); out.push_back(r); break;
    case Kind::AttributePoints: if (g.AttribPoint_GetAmount) guarded("reward ap", [&] { r.amount = g.AttribPoint_GetAmount(a); }); out.push_back(r); break;
    case Kind::DevotionPoints: if (g.Devotion_GetAmount) guarded("reward dp", [&] { r.amount = g.Devotion_GetAmount(a); }); out.push_back(r); break;
    case Kind::Levels: if (g.Level_GetAmount) guarded("reward lv", [&] { r.amount = g.Level_GetAmount(a); }); out.push_back(r); break;
    case Kind::Tribute: if (g.Tribute_GetAmount) guarded("reward tribute", [&] { r.amount = g.Tribute_GetAmount(a); }); out.push_back(r); break;
    case Kind::Reputation:
      if (g.Faction_GetAmount) guarded("reward rep", [&] { r.amount = g.Faction_GetAmount(a); });
      r.name = faction_name(a);
      out.push_back(r);
      break;
    case Kind::Item: {
      unsigned n = 0; int count = 1;
      if (g.Item_GetNumItems) guarded("reward nitems", [&] { n = g.Item_GetNumItems(a); });
      if (g.Item_GetCount) guarded("reward count", [&] { count = g.Item_GetCount(a); });
      if (n > 16) n = 16;
      for (unsigned i = 0; i < n; ++i) {
        void* item = nullptr;
        if (g.Item_GetInfoItem) guarded("reward item", [&] { item = g.Item_GetInfoItem(a, i); });
        if (!item) continue;
        Reward it{Kind::Item, count > 0 ? count : 1, gd::gameapi::item_name(item)};
        if (it.name.empty()) it.name = std::string(gd::strings::kUnknownItem);
        out.push_back(std::move(it));
      }
      if (!n) { r.name = std::string(gd::strings::kUnknownItem); r.amount = count; out.push_back(r); }
      break;
    }
    default: break;
  }
}

// ---- hooks ----
// Quest2Task::Complete(bool): the task's reward events run inside; everything they hand out is one record.
typedef void (*TaskComplete_t)(void*, bool);
static TaskComplete_t TaskComplete_hook_orig;
static void TaskComplete_hook(void* task, bool b) {
  load();
  ++g_completes;
  g_pending.emplace_back();
  TaskComplete_hook_orig(task, b);
  std::vector<Reward> rewards = std::move(g_pending.back());
  g_pending.pop_back();
  Record rec;
  rec.frame = gd::hooks::frame();
  guarded("reward record", [&] {
    if (g.Task_GetName) rec.task = u16_text(g.Task_GetName(task));
    const void* q = g.Task_GetQuest ? g.Task_GetQuest(task) : nullptr;
    if (q && g.Quest2_GetName) rec.quest = u16_text(g.Quest2_GetName(q));
    if (q && g.Quest2_IsComplete) rec.quest_complete = g.Quest2_IsComplete(q, false);
  });
  rec.rewards = std::move(rewards);
  std::string lines;
  for (const Reward& r : rec.rewards) lines += " [" + r.text() + "]";
  log::writef("quest_rewards: '{}' task '{}' complete={} rewards={}{}", rec.quest, rec.task, rec.quest_complete, rec.rewards.size(), lines);
  // A completion that handed out nothing is not a reward: the Bounty Table's "Report in" step completes EVERY other
  // bounty's Turn In task too (21 empty completions for one real one, live 2026-09-06), and they must not push the
  // real record out.
  if (rec.rewards.empty()) return;
  std::lock_guard lk(g_mu);
  g_records.push_back(std::move(rec));
  while (g_records.size() > 32) g_records.erase(g_records.begin());
}
// ScriptableActionCollection::Execute(Entity*): inside a task completion, read the reward actions before they
// run (a GiveItem's items were generated by its OnActivate a moment earlier and are the ones about to be given).
typedef void (*CollectionExecute_t)(const void*, void*);
static CollectionExecute_t CollectionExecute_hook_orig;
static void CollectionExecute_hook(const void* self, void* entity) {
  if (!g_pending.empty() && g.Collection_GetActions && g.IsReward) {
    ++g_collections;
    std::vector<void*> actions;
    guarded("reward actions", [&] { actions = vec_items<void*>(g.Collection_GetActions(self), 64); });
    for (void* a : actions) {
      if (!a) continue;
      bool reward = false;
      guarded("reward isreward", [&] { reward = g.IsReward(a); });
      if (reward) read_action(a, g_pending.back());
    }
  }
  CollectionExecute_hook_orig(self, entity);
}
}  // namespace

std::string Reward::text() const {
  MessageBuilder m;
  switch (kind) {
    case Kind::Experience: m.fragment(std::format("{}", amount)).fragment(gd::strings::kExperience); break;
    case Kind::Money: m.fragment(std::format("{}", amount)).fragment(gd::strings::kIronBits); break;
    case Kind::Item: if (amount > 1) m.fragment(std::format("{}", amount)); m.fragment(name); break;
    case Kind::SkillPoints: m.fragment(std::format("{}", amount)).fragment(amount == 1 ? gd::strings::kSkillPoint : gd::strings::kSkillPoints); break;
    case Kind::AttributePoints: m.fragment(std::format("{}", amount)).fragment(amount == 1 ? gd::strings::kAttributePoint : gd::strings::kAttributePoints); break;
    case Kind::DevotionPoints: m.fragment(std::format("{}", amount)).fragment(amount == 1 ? gd::strings::kDevotionPoint : gd::strings::kDevotionPoints); break;
    case Kind::Levels: m.fragment(std::format("{}", amount)).fragment(amount == 1 ? gd::strings::kLevel : gd::strings::kLevels); break;
    case Kind::Tribute: m.fragment(std::format("{}", amount)).fragment(gd::strings::kTribute); break;
    case Kind::Reputation: m.fragment(std::format("{}", amount)).fragment(name).fragment(gd::strings::kReputation); break;   // "125 Devil's Crossing reputation": one value and its unit, no comma inside (the user, 2026-09-06)
    default: m.fragment(gd::strings::kReward); break;
  }
  return m.build();
}

bool install() {
  g_hooks = {GD_HOOK(Quest2Task_Complete, TaskComplete_hook), GD_HOOK(ScriptableActionCollection_Execute, CollectionExecute_hook)};
  return gd::hooks::attach_hooks(g_hooks) == 0;
}
void remove() { gd::hooks::detach_hooks(g_hooks); }

std::vector<Record> recent() { std::lock_guard lk(g_mu); return g_records; }
bool latest_for(const std::string& quest, Record& out) {
  std::lock_guard lk(g_mu);
  for (size_t i = g_records.size(); i-- > 0;)
    if (g_records[i].quest == quest) { out = g_records[i]; return true; }
  return false;
}
std::string status() {
  std::string out = std::format("completes={} collections={} records={}\n", g_completes, g_collections, recent().size());
  for (const Record& r : recent()) {
    out += std::format("  frame {} '{}' task '{}' quest_complete={}\n", r.frame, r.quest, r.task, r.quest_complete);
    for (const Reward& w : r.rewards) out += std::format("    kind={} amount={} name='{}' -> {}\n", (int)w.kind, w.amount, w.name, w.text());
  }
  return out;
}
}  // namespace gd::quest_rewards
