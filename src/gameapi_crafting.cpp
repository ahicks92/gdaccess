// Crafting formulas (docs/crafting.md; static RE docs/re_crafting_gamedll.md, 2026-08-29): a formula is an
// ItemArtifactFormula item; the getters here are the ones the game's crafting window calls itself -- the result
// (an unrolled template item, so its tooltip reads stat ranges), the seven reagent slots (Base = the big left
// slot, then 1..6) with need / have (bags + materials + both stashes), the iron-bits cost and "[N]" =
// GetMaximumCraftable (virtual; called through the export after an is-a check).
#include "gameapi.h"
#include "gameapi_internal.h"
#include "core/strings.h"
#include "world.h"
#include "hooks.h"
#include <functional>
#include <cstring>
#include <format>

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
typedef unsigned (*IdFn)(const void*);
typedef const MsvcStringW* (*NameFn)(const void*, bool);
typedef int (*NeedFn)(const void*);
typedef int (*HaveFn)(const void*, const void*, bool);
struct Slot { IdFn id = nullptr; NameFn name = nullptr; NeedFn need = nullptr; HaveFn have = nullptr; };
struct Api {
  unsigned (*GetArtifact)(const void*) = nullptr;
  unsigned (*GetCreationCost)(const void*, const void*) = nullptr;   // (formula, Character const*)
  int (*GetMaximumCraftable)(void*, const void*) = nullptr;           // (formula, Character const*) -- virtual, export = the class's own body
  const void* (*FormulaClassInfo)() = nullptr;
  int (*GetDropClassification)(const void*) = nullptr;
  // the blacksmith's bonus (docs/re_crafting_gamedll.md 5): crafter -> table record -> a LootRandomizerTable object
  // (ObjectManager::CreateObjectFromFile, destroyed after) -> entries {record, weight} -> AttributeRange text
  const MsvcStringA* (*GetEnhancementTableName)(const void*) = nullptr;
  const MemVec* (*GetEnhancementTags)(const void*) = nullptr;
  void* (*ObjectManagerGet)() = nullptr;
  void* (*CreateObjectFromFile)(void*, const MsvcStringA*, unsigned, bool) = nullptr;
  void (*DestroyObjectEx)(void*, void*, const char*, int) = nullptr;
  const MemVec* (*GetAllEntries)(const void*) = nullptr;
  const void* (*TableClassInfo)() = nullptr;
  void* (*AttributeRange_ctor)(void*) = nullptr;
  void (*AttributeRange_dtor)(void*) = nullptr;
  void (*LoadAffix)(void*, const MsvcStringA*) = nullptr;
  void (*CreateText)(const void*, MemVec*, MemVec*, unsigned) = nullptr;
  Slot slots[7];
  bool loaded = false;
} g;
void load_crafting() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, GetArtifact, ItemArtifactFormula_GetArtifact);
  GAPI_LOAD(g, GetCreationCost, ItemArtifactFormula_GetCreationCost);
  GAPI_LOAD(g, GetMaximumCraftable, ItemArtifactFormula_GetMaximumCraftable);
  GAPI_LOAD(g, GetDropClassification, Item_GetDropClassification);
  g.FormulaClassInfo = fn<const void* (*)()>(ItemArtifactFormula_GetStaticClassInfo_DLL, ItemArtifactFormula_GetStaticClassInfo);
  GAPI_LOAD(g, GetEnhancementTableName, NpcCrafter_GetEnhancementTableName);
  GAPI_LOAD(g, GetEnhancementTags, NpcCrafter_GetEnhancementTags);
  GAPI_LOAD(g, ObjectManagerGet, ObjectManager_Get);
  GAPI_LOAD(g, CreateObjectFromFile, ObjectManager_CreateObjectFromFile);
  GAPI_LOAD(g, DestroyObjectEx, ObjectManager_DestroyObjectEx);
  GAPI_LOAD(g, GetAllEntries, LootRandomizerTable_GetAllEntries);
  g.TableClassInfo = fn<const void* (*)()>(LootRandomizerTable_GetStaticClassInfo_DLL, LootRandomizerTable_GetStaticClassInfo);
  GAPI_LOAD(g, AttributeRange_ctor, AttributeRange_ctor);
  GAPI_LOAD(g, AttributeRange_dtor, AttributeRange_dtor);
  GAPI_LOAD(g, LoadAffix, AttributeRange_LoadAffix);
  GAPI_LOAD(g, CreateText, AttributeRange_CreateText);
#define SLOT(i, N) \
  GAPI_LOAD(g, slots[i].id, ItemArtifactFormula_GetReagent##N##Id); \
  GAPI_LOAD(g, slots[i].name, ItemArtifactFormula_GetReagent##N##DisplayName); \
  GAPI_LOAD(g, slots[i].need, ItemArtifactFormula_GetReagent##N##QuantityForFormula); \
  GAPI_LOAD(g, slots[i].have, ItemArtifactFormula_GetReagent##N##Count)
  SLOT(0, Base); SLOT(1, 1); SLOT(2, 2); SLOT(3, 3); SLOT(4, 4); SLOT(5, 5); SLOT(6, 6);
#undef SLOT
}
}  // namespace

// The crafted item announces itself: Player::GiveArtifactToCharacter(Item*) is where the craft command hands the
// finished (rolled) item to the bag. Hooked once (src/dllmain.cpp), the listener runs after the game's own body,
// on the game thread, with the item already in the inventory -- no polling, no tick counting.
namespace {
typedef void (*GiveArtifact_t)(void*, void*);
GiveArtifact_t GiveArtifact_hook_orig = nullptr;
std::function<void(void*)> g_craft_listener;
void GiveArtifact_hook(void* player_, void* item) {
  GiveArtifact_hook_orig(player_, item);
  if (g_craft_listener) g_craft_listener(item);
}
std::vector<gd::hooks::Hook> g_craft_hooks;
}  // namespace
bool install_crafting_hooks() {
  g_craft_hooks = {GD_HOOK(Player_GiveArtifactToCharacter, GiveArtifact_hook)};
  return gd::hooks::attach_hooks(g_craft_hooks) == 0;
}
void remove_crafting_hooks() { gd::hooks::detach_hooks(g_craft_hooks); g_craft_listener = nullptr; }
void set_craft_listener(std::function<void(void*)> fn) { g_craft_listener = std::move(fn); }

bool is_formula(const void* obj) {
  load_crafting();
  return obj && g.FormulaClassInfo && world::object_is_a(obj, g.FormulaClassInfo());
}
std::optional<FormulaInfo> formula_info(unsigned formula_id) {
  load_crafting();
  void* f = object_by_id(formula_id);
  void* p = player();
  if (!f || !p || !is_formula(f)) return std::nullopt;
  FormulaInfo out;
  out.id = formula_id;
  bool ok = guarded("formula getters", [&] {
    if (g.GetArtifact) out.result_id = g.GetArtifact(f);
    if (g.GetCreationCost) out.cost = g.GetCreationCost(f, p);
    if (g.GetMaximumCraftable) out.max_craftable = g.GetMaximumCraftable(f, p);
    for (int i = 0; i < 7; ++i) {
      const Slot& s = g.slots[i];
      if (!s.id || !s.name || !s.need || !s.have) continue;
      unsigned id = s.id(f);
      if (!id) continue;
      Reagent r;
      r.id = id;
      r.name = u16_text(s.name(f, false));
      r.need = s.need(f);
      r.have = s.have(f, p, false);
      out.reagents.push_back(std::move(r));
    }
  });
  if (!ok) return std::nullopt;
  if (void* res = out.result_id ? object_by_id(out.result_id) : nullptr) {
    out.result_name = item_name(res);
    if (g.GetDropClassification) guarded("GetDropClassification", [&] { out.result_classification = g.GetDropClassification(res); });
  }
  return out;
}
// A std::string the game can read by const& (heap text; the game never frees an argument).
struct AString {
  MsvcStringA s{};
  std::string keep;
  explicit AString(std::string text) : keep(std::move(text)) {
    init_a(s);
    if (keep.size() < 16) { memcpy(s.u.buf, keep.c_str(), keep.size() + 1); s.size = keep.size(); }
    else { s.u.ptr = keep.data(); s.size = keep.size(); s.capacity = keep.size(); }
  }
};
std::optional<CrafterBonus> crafter_bonus(unsigned npc_id) {
  load_crafting();
  void* npc = object_by_id(npc_id);
  if (!npc || !g.GetEnhancementTableName || !g.GetEnhancementTags) return std::nullopt;
  CrafterBonus out;
  std::string record;
  bool ok = guarded("crafter bonus tags", [&] {
    record = a_text(g.GetEnhancementTableName(npc));
    for (const MsvcStringA& t : vec_items<MsvcStringA>(g.GetEnhancementTags(npc), 8)) {
      std::string tag = a_text(&t);
      std::string text = tag.empty() ? std::string() : localize(tag);
      if (!text.empty() && text.find_first_not_of(' ') != std::string::npos) out.blurb.push_back(text);
    }
  });
  if (!ok) return std::nullopt;
  if (record.empty() || !g.ObjectManagerGet || !g.CreateObjectFromFile || !g.GetAllEntries || !g.TableClassInfo || !g.AttributeRange_ctor || !g.LoadAffix || !g.CreateText) return out;
  guarded("crafter bonus table", [&] {
    void* om = g.ObjectManagerGet();
    AString rec(record);
    void* table = om ? g.CreateObjectFromFile(om, &rec.s, 0, true) : nullptr;
    if (!table) return;
    if (world::object_is_a(table, g.TableClassInfo())) {
      // entries: mem::vector<std::pair<std::string, unsigned>> (stride 0x28: the string, then the weight)
      struct Entry { MsvcStringA record; unsigned weight; unsigned pad; };
      static_assert(sizeof(Entry) == 0x28);
      for (const Entry& e : vec_items<Entry>(g.GetAllEntries(table), 16)) {
        alignas(16) unsigned char ar[0x800] = {};
        g.AttributeRange_ctor(ar);
        g.LoadAffix(ar, &e.record);
        MsvcStringW lines[16]; MemVec v{lines, lines, lines + 16};
        for (MsvcStringW& l : lines) init_u16(l);
        g.CreateText(ar, &v, &v, 1);
        size_t n = ((char*)v.end - (char*)v.begin) / sizeof(MsvcStringW);
        std::string joined;
        for (size_t i = 0; i < n && i < 64; ++i) {
          std::string t = take_u16(((MsvcStringW*)v.begin)[i]);
          if (t.empty()) continue;
          if (!joined.empty()) joined += " ";
          joined += t;
        }
        if (v.begin != lines && v.begin) free(v.begin);   // the game grew it through its allocator (process heap)
        if (!joined.empty()) out.entries.push_back(joined);
        if (g.AttributeRange_dtor) g.AttributeRange_dtor(ar);
      }
    }
    if (g.DestroyObjectEx) g.DestroyObjectEx(om, table, "gdaccess", 0);
  });
  return out;
}
std::string dump_formula(unsigned formula_id) {
  std::optional<FormulaInfo> fi = formula_info(formula_id);
  if (!fi) return std::format("formula {}: not a formula / no object\n", formula_id);
  std::string out = std::format("formula {} result={} '{}' class={} cost={} max={}\n", fi->id, fi->result_id, fi->result_name, fi->result_classification, fi->cost, fi->max_craftable);
  for (const Reagent& r : fi->reagents) out += std::format("  reagent id={} '{}' {}/{} record='{}'\n", r.id, r.name, r.have, r.need, object_record(object_by_id(r.id)));
  out += std::format("  result record='{}'\n", object_record(object_by_id(fi->result_id)));
  return out;
}
}  // namespace gd::gameapi
