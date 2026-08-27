// Bags, equipment and item text: PlayerInventoryCtrl / InventorySack / EquipmentCtrl / Item exports
// (docs/ingame-ui-survey.md; the exe's own click sequences from the 2026-08-22 readout). Item text builders
// are virtual and dispatched through the item's own vtable.
#include "gameapi.h"
#include "gameapi_internal.h"
#include <algorithm>
#include <format>

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
struct Api {
  void* (*GetInventoryCtrl)(const void*) = nullptr;
  unsigned (*GetNumberOfSacks)(const void*) = nullptr;
  unsigned (*GetSelectedSackNumber)(const void*) = nullptr;
  void (*SetSelectedSackNumber)(void*, unsigned) = nullptr;
  void* (*GetSack)(void*, int) = nullptr;
  void (*UseItem)(void*, unsigned, int) = nullptr;
  bool (*Inv_RemoveItem)(void*, unsigned, bool) = nullptr;
  bool (*Inv_AddItem)(void*, unsigned, bool, bool) = nullptr;
  bool (*Inv_IsSpaceAvailable)(const void*, const void*) = nullptr;
  const void* (*Sack_GetInventory)(const void*) = nullptr;
  unsigned (*Sack_GetGridWidth)(const void*) = nullptr;
  unsigned (*Sack_GetGridHeight)(const void*) = nullptr;
  const MsvcStringW* (*Sack_GetButtonName)(const void*) = nullptr;
  void* (*GetEquipmentCtrl)(const void*) = nullptr;
  unsigned (*Equip_GetItemId)(const void*, int) = nullptr;
  MsvcStringA* (*Equip_GetEquipmentLocationTag)(MsvcStringA*, int) = nullptr;   // static: hidden pointer first
  bool (*Equip_GetIsAlternate)(const void*) = nullptr;
  unsigned (*Equip_PlaceItem)(void*, int, unsigned, bool, bool) = nullptr;
  bool (*Equip_RemoveItem)(void*, unsigned) = nullptr;
  bool (*Equip_SmartAutoInsert)(void*, unsigned, MemVec*, bool) = nullptr;
  bool (*Equip_CanItemBePlaced)(const void*, int, unsigned) = nullptr;
  bool (*Ctrl_GetAlternateEquipment)(const void*) = nullptr;   // the active weapon set (A/B) on the controller
  void (*Ctrl_SetAlternateEquipment)(void*, bool) = nullptr;   // swap the active weapon set (only the two hands change)
  unsigned (*GetCurrentMoney)(const void*) = nullptr;
  MsvcStringW* (*ShrineOffering1)(const void*, MsvcStringW*) = nullptr;   // StaticShrine::GetOffering1..3DisplayName (u16 by value, hidden pointer 2nd)
  MsvcStringW* (*ShrineOffering2)(const void*, MsvcStringW*) = nullptr;
  MsvcStringW* (*ShrineOffering3)(const void*, MsvcStringW*) = nullptr;
  void (*AddMoney)(void*, unsigned) = nullptr;   // dev: Character::AddMoney (iron bits)
  void (*SendDropItemRandom)(void*, unsigned) = nullptr;
  void (*PickupItem)(void*, unsigned) = nullptr;               // virtual on the controller; the export is the implementation
  void (*GetCompatibleItems)(void*, unsigned, MemVec*) = nullptr;   // Player: item ids a component can attach to (bags+equipped+stash)
  void (*Character_UseItemOn)(void*, unsigned, unsigned, int, unsigned, unsigned, bool) = nullptr;  // attach used->target (ItemSource)
  // merchants / caravan
  const void* (*GetMarketInventorySack)(const void*, unsigned, int) = nullptr;
  bool (*PlayerPurchaseRequest)(void*, unsigned, unsigned) = nullptr;
  bool (*PlayerSaleRequest)(void*, unsigned, unsigned, bool) = nullptr;
  void (*CreateUIPlayerBuyText)(const void*, unsigned, unsigned, MemVec*) = nullptr;
  void (*CreateUIPlayerSellText)(const void*, unsigned, unsigned, MemVec*) = nullptr;
  void (*SendRemoveItemFromInventory)(void*, unsigned) = nullptr;
  void (*SendAddItemToInventory)(void*, unsigned) = nullptr;
  void* (*Item_CreateItem)(const void*) = nullptr;                 // static: a new Item from an ItemReplicaInfo
  void (*Item_SetStackSize)(void*, unsigned) = nullptr;
  void (*SendUpdateItemStack)(void*, unsigned, unsigned) = nullptr;  // ControllerCharacter: item id, new count
  void* (*ObjectManager_Get)() = nullptr;
  void (*ObjectManager_DestroyObjectEx)(void*, void*, const char*, int) = nullptr;
  unsigned (*GetItemMaxStackSize)(const void*) = nullptr;
  const MemVec* (*GetPrivateStash)(void*) = nullptr;
  const MemVec* (*GetPlayerTransfer)(void*) = nullptr;
  bool (*AddItemToPrivateStash)(void*, unsigned, unsigned, bool) = nullptr;
  bool (*RemoveItemFromPrivateStash)(void*, unsigned) = nullptr;
  unsigned (*GetSelectedStashSackNumber)(const void*) = nullptr;
  bool (*AddItemToTransfer)(void*, unsigned, unsigned, bool) = nullptr;
  bool (*RemoveItemFromTransfer)(void*, unsigned) = nullptr;
  unsigned (*GetSelectedTransferSackNumber)(const void*) = nullptr;
  bool (*IsTransferOpen)(const void*) = nullptr;
  // Item virtuals (base implementations; slots found in the vftables)
  void (*Item_GetUIDisplayText)(const void*, const void*, MemVec*, bool) = nullptr;
  void (*Item_GetSimpleUIDisplayText)(const void*, const void*, MemVec*) = nullptr;
  unsigned (*Item_GetStackSize)(const void*) = nullptr;
  bool (*Item_AreRequirementsMet)(const void*, const void*) = nullptr;
  MsvcStringW* (*Item_GetGameDescription)(const void*, MsvcStringW*, bool, bool) = nullptr;
  void** Item_vftable = nullptr;
  void** Item_vftable_plain = nullptr;
  bool loaded = false;
} g;
int g_s_ui = -1, g_s_simple = -1, g_s_stack = -1, g_s_req = -1, g_s_desc = -1;
int slot_in(const void* f) { int s = vslot(g.Item_vftable, f); return s >= 0 ? s : vslot(g.Item_vftable_plain, f); }
constexpr int kBagSource = 1;   // ItemSource: 1 bag, 2 private stash, 3 transfer, 4 trade, 5 station slot, 7 caravan reagents

void load_items() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, GetInventoryCtrl, ControllerPlayer_GetInventoryCtrl);
  GAPI_LOAD(g, GetNumberOfSacks, InvCtrl_GetNumberOfSacks);
  GAPI_LOAD(g, GetSelectedSackNumber, InvCtrl_GetSelectedSackNumber);
  GAPI_LOAD(g, SetSelectedSackNumber, InvCtrl_SetSelectedSackNumber);
  GAPI_LOAD(g, GetSack, InvCtrl_GetSack);
  GAPI_LOAD(g, UseItem, InvCtrl_UseItem);
  GAPI_LOAD(g, Inv_RemoveItem, InvCtrl_RemoveItem);
  GAPI_LOAD(g, Inv_AddItem, InvCtrl_AddItem);
  GAPI_LOAD(g, Inv_IsSpaceAvailable, InvCtrl_IsSpaceAvailable);
  GAPI_LOAD(g, Sack_GetInventory, InventorySack_GetInventory);
  GAPI_LOAD(g, Sack_GetGridWidth, InventorySack_GetGridWidth);
  GAPI_LOAD(g, Sack_GetGridHeight, InventorySack_GetGridHeight);
  GAPI_LOAD(g, Sack_GetButtonName, InventorySack_GetButtonName);
  GAPI_LOAD(g, GetEquipmentCtrl, ControllerCharacter_GetEquipmentCtrl);
  GAPI_LOAD(g, Equip_GetItemId, EquipmentCtrl_GetItemId);
  GAPI_LOAD(g, Equip_GetEquipmentLocationTag, EquipmentCtrl_GetEquipmentLocationTag);
  GAPI_LOAD(g, Equip_GetIsAlternate, EquipmentCtrl_GetIsAlternate);
  GAPI_LOAD(g, Equip_PlaceItem, EquipmentCtrl_PlaceItem);
  GAPI_LOAD(g, Equip_RemoveItem, EquipmentCtrl_RemoveItem);
  GAPI_LOAD(g, Equip_SmartAutoInsert, EquipmentCtrl_SmartAutoInsert);
  GAPI_LOAD(g, Equip_CanItemBePlaced, EquipmentCtrl_CanItemBePlaced);
  GAPI_LOAD(g, Ctrl_GetAlternateEquipment, ControllerCharacter_GetAlternateEquipment);
  GAPI_LOAD(g, Ctrl_SetAlternateEquipment, ControllerCharacter_SetAlternateEquipment);
  GAPI_LOAD(g, GetCurrentMoney, Character_GetCurrentMoney);
  GAPI_LOAD(g, AddMoney, Character_AddMoney);
  GAPI_LOAD(g, ShrineOffering1, StaticShrine_GetOffering1DisplayName);
  GAPI_LOAD(g, ShrineOffering2, StaticShrine_GetOffering2DisplayName);
  GAPI_LOAD(g, ShrineOffering3, StaticShrine_GetOffering3DisplayName);
  GAPI_LOAD(g, SendDropItemRandom, ControllerCharacter_SendDropItemRandom);
  GAPI_LOAD(g, PickupItem, ControllerCharacter_PickupItem);
  GAPI_LOAD(g, GetCompatibleItems, Player_GetCompatibleItems);
  GAPI_LOAD(g, Character_UseItemOn, Character_UseItemOn);
  GAPI_LOAD(g, GetMarketInventorySack, GameEngine_GetMarketInventorySack);
  GAPI_LOAD(g, PlayerPurchaseRequest, GameEngine_PlayerPurchaseRequest);
  GAPI_LOAD(g, PlayerSaleRequest, GameEngine_PlayerSaleRequest);
  GAPI_LOAD(g, CreateUIPlayerBuyText, GameEngine_CreateUIPlayerBuyText);
  GAPI_LOAD(g, CreateUIPlayerSellText, GameEngine_CreateUIPlayerSellText);
  GAPI_LOAD(g, SendRemoveItemFromInventory, ControllerCharacter_SendRemoveItemFromInventory);
  GAPI_LOAD(g, SendAddItemToInventory, ControllerCharacter_SendAddItemToInventory);
  GAPI_LOAD(g, Item_CreateItem, Item_CreateItem);
  GAPI_LOAD(g, Item_SetStackSize, Item_SetStackSize);
  GAPI_LOAD(g, SendUpdateItemStack, ControllerCharacter_SendUpdateItemStack);
  GAPI_LOAD(g, ObjectManager_Get, ObjectManager_Get);
  GAPI_LOAD(g, ObjectManager_DestroyObjectEx, ObjectManager_DestroyObjectEx);
  GAPI_LOAD(g, GetItemMaxStackSize, GameEngine_GetItemMaxStackSize);
  GAPI_LOAD(g, GetPrivateStash, Player_GetPrivateStash);
  GAPI_LOAD(g, GetPlayerTransfer, GameEngine_GetPlayerTransfer);
  GAPI_LOAD(g, AddItemToPrivateStash, Player_AddItemToPrivateStash);
  GAPI_LOAD(g, RemoveItemFromPrivateStash, Player_RemoveItemFromPrivateStash);
  GAPI_LOAD(g, GetSelectedStashSackNumber, Player_GetSelectedStashSackNumber);
  GAPI_LOAD(g, AddItemToTransfer, GameEngine_AddItemToTransfer);
  GAPI_LOAD(g, RemoveItemFromTransfer, GameEngine_RemoveItemFromTransfer);
  GAPI_LOAD(g, GetSelectedTransferSackNumber, GameEngine_GetSelectedTransferSackNumber);
  GAPI_LOAD(g, IsTransferOpen, GameEngine_IsTransferOpen);
  GAPI_LOAD(g, Item_GetUIDisplayText, Item_GetUIDisplayText);
  GAPI_LOAD(g, Item_GetSimpleUIDisplayText, Item_GetSimpleUIDisplayText);
  GAPI_LOAD(g, Item_GetStackSize, Item_GetStackSize);
  GAPI_LOAD(g, Item_AreRequirementsMet, Item_AreRequirementsMet);
  GAPI_LOAD(g, Item_GetGameDescription, Item_GetGameDescription);
  GAPI_LOAD(g, Item_vftable, Item_vftable);
  GAPI_LOAD(g, Item_vftable_plain, Item_vftable_plain);
  g_s_ui = slot_in((const void*)g.Item_GetUIDisplayText);
  g_s_simple = slot_in((const void*)g.Item_GetSimpleUIDisplayText);
  g_s_stack = slot_in((const void*)g.Item_GetStackSize);
  g_s_req = slot_in((const void*)g.Item_AreRequirementsMet);
  g_s_desc = slot_in((const void*)g.Item_GetGameDescription);
  log::writef("gameapi: Item slots ui={} simple={} stack={} req={} desc={}", g_s_ui, g_s_simple, g_s_stack, g_s_req, g_s_desc);
}
void* inv_ctrl() { load_items(); void* c = controller(); return c && g.GetInventoryCtrl ? g.GetInventoryCtrl(c) : nullptr; }
void* equip_ctrl() { load_items(); void* c = controller(); return c && g.GetEquipmentCtrl ? g.GetEquipmentCtrl(c) : nullptr; }
}  // namespace

std::string item_name(const void* item) {
  load_items();
  std::string out;
  if (!item) return out;
  auto f = (MsvcStringW * (*)(const void*, MsvcStringW*, bool, bool))vfn(item, g_s_desc);
  if (!f) f = g.Item_GetGameDescription;
  if (!f) return out;
  guarded("Item::GetGameDescription", [&] { MsvcStringW s; init_u16(s); f(item, &s, false, false); out = take_u16(s); });
  return out;
}
unsigned item_stack(const void* item) {
  load_items();
  unsigned n = 0;
  auto f = (unsigned (*)(const void*))(item ? vfn(item, g_s_stack) : nullptr);
  if (f) guarded("Item::GetStackSize", [&] { n = f(item); });
  return n;
}
bool item_requirements_met(const void* item) {
  load_items();
  bool ok = true; void* p = player();
  auto f = (bool (*)(const void*, const void*))(item ? vfn(item, g_s_req) : nullptr);
  if (f && p) guarded("Item::AreRequirementsMet", [&] { ok = f(item, p); });
  return ok;
}
std::vector<std::string> item_tooltip(const void* item, bool simple, bool details) {
  load_items();
  std::vector<std::string> out;
  void* p = player();
  if (!item || !p) return out;
  TextLineBuffer buf;
  if (simple) { auto f = (void (*)(const void*, const void*, MemVec*))vfn(item, g_s_simple); if (f) guarded("Item::GetSimpleUIDisplayText", [&] { f(item, p, buf.vec()); }); }
  else { auto f = (void (*)(const void*, const void*, MemVec*, bool))vfn(item, g_s_ui); if (f) guarded("Item::GetUIDisplayText", [&] { f(item, p, buf.vec(), details); }); }   // details = the Ctrl-held form
  // The mouse hint lines ("[Hold Ctrl to Show Details]" / "[Release Ctrl to Hide Details]") are UI, not item text.
  const std::string hint_show = localize("tagShowDetails"), hint_hide = localize("tagHideDetails");
  for (TextLine& l : buf.take("item text")) {
    if (!hint_show.empty() && l.text == hint_show) continue;
    if (!hint_hide.empty() && l.text == hint_hide) continue;
    out.push_back(std::move(l.text));
  }
  return out;
}

std::vector<Bag> bags() {
  std::vector<Bag> out;
  void* ic = inv_ctrl();
  if (!ic || !g.GetNumberOfSacks || !g.GetSack || !g.Sack_GetInventory) return out;
  guarded("bags", [&] {
    unsigned n = g.GetNumberOfSacks(ic);
    for (unsigned i = 0; i < n && i < 8; ++i) {
      void* sack = g.GetSack(ic, (int)i);
      if (!sack) continue;
      Bag b{(int)i};
      b.name = g.Sack_GetButtonName ? u16_text(g.Sack_GetButtonName(sack)) : std::string();
      b.width = g.Sack_GetGridWidth ? g.Sack_GetGridWidth(sack) : 0;
      b.height = g.Sack_GetGridHeight ? g.Sack_GetGridHeight(sack) : 0;
      { const void* m = g.Sack_GetInventory(sack); b.debug = std::format(" map={} head={} size={}", m, rdp(m, 0), rd_or<size_t>(m, 8, 0)); }
      // mem::map<unsigned, Rect>: the pair follows the 0x1a-byte node header at 4-byte alignment -- key +0x1c,
      // Rect +0x20 (x, y, w, h in grid pixels; verified live 2026-08-22: the market map's pointer value sits at +0x28).
      for (void* node : map_nodes(g.Sack_GetInventory(sack))) {
        BagItem it{rd_or<unsigned>(node, 0x1c, 0)};
        float r[4] = {}; read_mem((char*)node + 0x20, r, sizeof r);
        it.x = r[0]; it.y = r[1]; it.w = r[2]; it.h = r[3];
        it.p = object_by_id(it.id);
        it.name = item_name(it.p);
        it.stack = item_stack(it.p);
        b.items.push_back(std::move(it));
      }
      std::stable_sort(b.items.begin(), b.items.end(), [](const BagItem& a, const BagItem& c) { return a.y != c.y ? a.y < c.y : a.x < c.x; });
      out.push_back(std::move(b));
    }
  });
  return out;
}
int selected_bag() { void* ic = inv_ctrl(); int s = -1; if (ic && g.GetSelectedSackNumber) guarded("GetSelectedSackNumber", [&] { s = (int)g.GetSelectedSackNumber(ic); }); return s; }
bool select_bag(int index) { void* ic = inv_ctrl(); return ic && g.SetSelectedSackNumber && index >= 0 && guarded("SetSelectedSackNumber", [&] { g.SetSelectedSackNumber(ic, (unsigned)index); }); }

std::vector<EquipSlot> equipment() {
  std::vector<EquipSlot> out;
  void* ec = equip_ctrl();
  if (!ec || !g.Equip_GetItemId || !g.Equip_GetEquipmentLocationTag) return out;
  // EquipmentCtrlLocation 1..14 (verified): Head, Neck, Chest, Legs, Feet, Ring1, Ring2, Hands, RightHand,
  // LeftHand, Relic, Waist, Shoulders, Medal. Presented in the paperdoll's reading order.
  static const int order[] = {9, 10, 1, 13, 3, 8, 12, 4, 5, 2, 6, 7, 11, 14};
  guarded("equipment", [&] {
    for (int loc : order) {
      EquipSlot s{loc};
      MsvcStringA tag; init_a(tag);
      g.Equip_GetEquipmentLocationTag(&tag, loc);
      s.label = localize(take_a(tag));
      if (s.label.size() > 2 && s.label.front() == '(' && s.label.back() == ')') s.label = s.label.substr(1, s.label.size() - 2);  // the tags read "(Right Hand)"
      s.item_id = g.Equip_GetItemId(ec, loc);
      s.item = s.item_id ? object_by_id(s.item_id) : nullptr;
      s.name = item_name(s.item);
      out.push_back(std::move(s));
    }
  });
  return out;
}
bool alternate_weapons() { void* ec = equip_ctrl(); bool a = false; if (ec && g.Equip_GetIsAlternate) guarded("GetIsAlternate", [&] { a = g.Equip_GetIsAlternate(ec); }); return a; }
// Whether an item can go in an equipment slot (EquipmentCtrl::CanItemBePlaced) -- the equip picker's filter.
bool can_equip(unsigned item_id, int loc) {
  void* ec = equip_ctrl();
  bool ok = false;
  if (ec && g.Equip_CanItemBePlaced && item_id) guarded("CanItemBePlaced", [&] { ok = g.Equip_CanItemBePlaced(ec, loc, item_id); });
  return ok;
}
// Swap the active weapon set (the two hands); returns the new state (true = alternate/set B). Everything else
// is shared. ControllerCharacter::SetAlternateEquipment propagates to the EquipmentCtrl, so equipment() then
// reads the new set's hands.
bool swap_weapon_set() {
  void* c = controller();
  if (!c || !g.Ctrl_GetAlternateEquipment || !g.Ctrl_SetAlternateEquipment) return alternate_weapons();
  bool nv = false;
  guarded("SetAlternateEquipment", [&] { nv = !g.Ctrl_GetAlternateEquipment(c); g.Ctrl_SetAlternateEquipment(c, nv); });
  log::writef("gameapi: swap weapon set -> alternate={}", nv);
  return nv;
}
// The offerings a ruined devotion shrine asks for: the game's own display names (empty slots skipped). The shrine
// record has no quantity field (one item per slot); the game's window draws these as three item boxes.
std::vector<std::string> shrine_offerings(unsigned shrine_id) {
  load_items();
  std::vector<std::string> out;
  void* s = object_by_id(shrine_id);
  if (!s) return out;
  for (auto f : {g.ShrineOffering1, g.ShrineOffering2, g.ShrineOffering3}) {
    if (!f) continue;
    std::string name;
    guarded("StaticShrine::GetOfferingDisplayName", [&] { MsvcStringW w; init_u16(w); f(s, &w); name = take_u16(w); });
    if (!name.empty()) out.push_back(name);
  }
  return out;
}
unsigned money() { void* p = player(); unsigned m = 0; load_items(); if (p && g.GetCurrentMoney) guarded("GetCurrentMoney", [&] { m = g.GetCurrentMoney(p); }); return m; }
bool dev_add_money(unsigned bits) { void* p = player(); load_items(); if (!p || !g.AddMoney) return false; bool ok = guarded("AddMoney", [&] { g.AddMoney(p, bits); }); log::writef("gameapi: dev add money {} ok={}", bits, ok); return ok; }

// The bag's right-click (exe+0x1eb1a0): a consumable goes through PlayerInventoryCtrl::UseItem(id, bag);
// equipment through SmartAutoInsert + removal from the bag + re-homing whatever got displaced (exe+0x1eb4c6).
bool use_item(unsigned id, int source) {
  void* ic = inv_ctrl(); void* ec = equip_ctrl();
  if (!ic || !id) return false;
  void* item = object_by_id(id);
  bool ok = false, equipped = false;
  if (ec && g.Equip_SmartAutoInsert && g.Inv_RemoveItem && g.Inv_AddItem && item && item_requirements_met(item)) {
    VecBuffer<unsigned> displaced(16);
    guarded("equip (SmartAutoInsert)", [&] {
      if (!g.Equip_SmartAutoInsert(ec, id, displaced.vec(), false)) return;
      equipped = true;
      g.Inv_RemoveItem(ic, id, true);
      for (unsigned d : displaced.take("SmartAutoInsert")) if (d) g.Inv_AddItem(ic, d, true, false);
      ok = true;
    });
  }
  if (!equipped && g.UseItem) ok = guarded("PlayerInventoryCtrl::UseItem", [&] { g.UseItem(ic, id, source > 0 ? source : kBagSource); });
  invalidate_objects();
  log::writef("gameapi: use item {} equipped={} ok={}", id, equipped, ok);
  return ok;
}
// A component/augment (records/items/materia -- Class ItemRelic; no ItemRelic ships outside that folder).
// Activating one opens the attach picker instead of "using" it.
bool is_component(unsigned id) {
  void* p = object_by_id(id);
  if (!p) return false;
  return object_record(p).find("/items/materia/") != std::string::npos;
}
// The item ids this component can attach to (the game's own union of bags + equipped + stash), via
// Player::GetCompatibleItems -- do not re-implement the per-target type test.
std::vector<unsigned> compatible_items(unsigned component_id) {
  load_items();
  std::vector<unsigned> out;
  void* p = player();
  if (!p || !g.GetCompatibleItems || !component_id) return out;
  VecBuffer<unsigned> buf(256);
  guarded("Player::GetCompatibleItems", [&] { g.GetCompatibleItems(p, component_id, buf.vec()); });
  out = buf.take("GetCompatibleItems");
  return out;
}
// Attach the component to the target item. Character::UseItemOn resolves both ids, is-a-checks the used item
// and applies + consumes it (a pure inventory op, no NPC). source: ItemSource of the component (bag by default).
bool attach_component(unsigned component_id, unsigned target_id, int source) {
  load_items();
  void* p = player();
  if (!p || !g.Character_UseItemOn || !component_id || !target_id) return false;
  bool ok = guarded("Character::UseItemOn", [&] {
    g.Character_UseItemOn(p, component_id, target_id, source > 0 ? source : kBagSource, 0u, 0u, false);
  });
  invalidate_objects();
  log::writef("gameapi: attach component {} -> item {} source {} ok={}", component_id, target_id, source, ok);
  return ok;
}
bool drop_item(unsigned id) {
  void* c = controller(); load_items();
  if (!c || !g.SendDropItemRandom || !id) return false;
  bool ok = guarded("SendDropItemRandom", [&] { g.SendDropItemRandom(c, id); });
  invalidate_objects();
  return ok;
}
// Unequip into the bag, in the exe's order (exe+0x1eb7f1): room check, AddItem to the bag, then the slot's
// RemoveItem (which only detaches -- on its own it orphans the item).
bool unequip(int loc) {
  void* ec = equip_ctrl(); void* ic = inv_ctrl();
  if (!ec || !ic || !g.Equip_GetItemId || !g.Equip_RemoveItem || !g.Inv_AddItem) return false;
  bool ok = false;
  guarded("unequip", [&] {
    unsigned id = g.Equip_GetItemId(ec, loc);
    if (!id) return;
    void* item = object_by_id(id);
    if (g.Inv_IsSpaceAvailable && item && !g.Inv_IsSpaceAvailable(ic, item)) return;
    if (!g.Inv_AddItem(ic, id, true, false)) return;
    ok = g.Equip_RemoveItem(ec, id);
  });
  invalidate_objects();
  log::writef("gameapi: unequip loc {} ok={}", loc, ok);
  return ok;
}
// Equip into a specific slot: validate, take it out of the bag, place, and re-home the displaced item.
bool equip(unsigned id, int loc) {
  void* ec = equip_ctrl(); void* ic = inv_ctrl();
  if (!ec || !ic || !g.Equip_PlaceItem || !id) return false;
  bool ok = false;
  guarded("equip into slot", [&] {
    if (g.Equip_CanItemBePlaced && !g.Equip_CanItemBePlaced(ec, loc, id)) return;
    if (g.Inv_RemoveItem) g.Inv_RemoveItem(ic, id, true);
    unsigned old = g.Equip_PlaceItem(ec, loc, id, false, false);
    if (old && g.Inv_AddItem) g.Inv_AddItem(ic, old, true, false);
    ok = true;
  });
  invalidate_objects();
  log::writef("gameapi: equip {} at {} ok={}", id, loc, ok);
  return ok;
}
// ControllerCharacter::PickupItem(id): the game's own pickup command (CharPickUpConfigCmd), no distance check.
bool pickup_item(unsigned id) {
  void* c = controller(); load_items();
  if (!c || !g.PickupItem || !id) return false;
  bool ok = guarded("PickupItem", [&] { g.PickupItem(c, id); });
  invalidate_objects();
  log::writef("gameapi: pickup {} ok={}", id, ok);
  return ok;
}

// ---- merchants ----
namespace {
Bag read_sack(const void* sack, int index) {
  Bag b{index};
  if (!sack) return b;
  guarded("read sack", [&] {
    b.name = g.Sack_GetButtonName ? u16_text(g.Sack_GetButtonName(sack)) : std::string();
    b.width = g.Sack_GetGridWidth ? g.Sack_GetGridWidth(sack) : 0;
    b.height = g.Sack_GetGridHeight ? g.Sack_GetGridHeight(sack) : 0;
    for (void* node : map_nodes(g.Sack_GetInventory(sack))) {
      BagItem it{rd_or<unsigned>(node, 0x1c, 0)};
      float r[4] = {}; read_mem((char*)node + 0x20, r, sizeof r);
      it.x = r[0]; it.y = r[1]; it.w = r[2]; it.h = r[3];
      it.p = object_by_id(it.id);
      it.name = item_name(it.p);
      it.stack = item_stack(it.p);
      b.items.push_back(std::move(it));
    }
    std::stable_sort(b.items.begin(), b.items.end(), [](const BagItem& a, const BagItem& c) { return a.y != c.y ? a.y < c.y : a.x < c.x; });
  });
  return b;
}
}  // namespace
// The merchant's stock per Market_TypeEnum (values unknown: 0..7 are probed, the non-empty ones kept).
// Dev: what the engine's market map holds and what market_stock(id) yields (the vendor screen's id read was
// garbage on the first live vendor, 2026-08-23: it used 340 for Kerrick, object id 19764).
std::string vendor_dump(unsigned id) {
  void* e = engine(); load_items();
  std::string out;
  if (!e) return "no engine\n";
  out += "market map (GameEngine+0x40e0) keys:";
  for (void* n : map_nodes((const char*)e + 0x40e0, 64))
    out += std::format(" {}->{}", rd_or<unsigned>(n, 0x20, 0), rdp(n, 0x28));
  out += "\n";
  if (id) {
    std::vector<MarketTab> tabs = market_stock(id);
    out += std::format("market_stock({}): {} tabs\n", id, tabs.size());
    for (const MarketTab& t : tabs) {
      out += std::format("  type {} '{}' {} items", t.type, t.name, t.items.size());
      if (!t.items.empty()) out += std::format("; first '{}' price '{}'", t.items[0].name, market_price_text(id, t.items[0].id, true));
      out += "\n";
    }
  }
  return out;
}
// The merchant's stock, one tab per Market_TypeEnum, in the game's own display order with the game's own
// captions. The enum values were read from each item class's GetItemMarketType (Game.dll, 2026-08-23):
// 1=Armor, 2=Melee, 3=Ranged, 4=Accessories/Consumables, 6=Artifacts. Types 0/5/7 are the buyback/fallback
// sack the engine returns for out-of-range values (all the same pointer) -- dropped here, we have our own
// Sell tab. Empty tabs (a merchant who does not stock a category) are skipped.
std::vector<MarketTab> market_stock(unsigned market_id) {
  std::vector<MarketTab> out;
  void* e = engine(); load_items();
  if (!e || !market_id || !g.GetMarketInventorySack) return out;
  static const struct { int type; const char* tag; } kTabs[] = {
    {2, "tagVendorTab01A"},          // Melee Weapons
    {3, "tagVendorTab02A"},          // Ranged Weapons
    {1, "tagVendorTab03A"},          // Armor
    {4, "tagVendorTab04A"},          // Accessories and Consumables
    {6, "tagVendorTabArtifactA"},    // Artifacts
  };
  for (const auto& t : kTabs) {
    const void* sack = nullptr;
    guarded("GetMarketInventorySack", [&] { sack = g.GetMarketInventorySack(e, market_id, t.type); });
    if (!sack) continue;
    Bag b = read_sack(sack, t.type);
    if (b.items.empty()) continue;
    out.push_back({t.type, localize(t.tag), std::move(b.items)});
  }
  return out;
}
std::string market_price_text(unsigned market_id, unsigned item_id, bool buying) {
  void* e = engine(); load_items();
  if (!e || !market_id || !item_id) return {};
  TextLineBuffer buf;
  auto f = buying ? g.CreateUIPlayerBuyText : g.CreateUIPlayerSellText;
  if (!f) return {};
  guarded("market price text", [&] { f(e, market_id, item_id, buf.vec()); });
  std::string out;
  for (TextLine& l : buf.take("price text")) { if (!out.empty()) out += ", "; out += l.text; }
  return out;
}
bool buy(unsigned market_id, unsigned item_id) {
  void* e = engine(); load_items();
  if (!e || !g.PlayerPurchaseRequest) return false;
  bool r = false;
  guarded("PlayerPurchaseRequest", [&] { r = g.PlayerPurchaseRequest(e, market_id, item_id); });
  invalidate_objects();
  log::writef("gameapi: buy {} from {} -> {}", item_id, market_id, r);
  return r;
}
// The bag's right-click at a vendor (exe+0x1eb226): PlayerSaleRequest, then the item leaves the bag.
bool sell(unsigned market_id, unsigned item_id) {
  void* e = engine(); void* ic = inv_ctrl(); void* c = controller();
  if (!e || !ic || !c || !g.PlayerSaleRequest || !g.Inv_RemoveItem || !g.SendRemoveItemFromInventory) return false;
  bool r = false;
  guarded("PlayerSaleRequest", [&] {
    r = g.PlayerSaleRequest(e, market_id, item_id, false);
    if (r) { g.Inv_RemoveItem(ic, item_id, true); g.SendRemoveItemFromInventory(c, item_id); }
  });
  invalidate_objects();
  log::writef("gameapi: sell {} to {} -> {}", item_id, market_id, r);
  return r;
}
// Part of a stack. The exe's stack-split window's OK (exe+0x1dcb70, read 2026-08-26) clones the item from a
// copy of its ItemReplicaInfo (inline at Item+0x538, 0x190 bytes: +0 = object id, 0 = allocate a fresh one;
// +0x178 = count) with the static Item::CreateItem, tells the character about it (SendAddItemToInventory --
// the cursor's only lasting effect; the clone is NOT placed in the bag grid: PlayerInventoryCtrl::AddItem
// merges a stackable back into its source stack and destroys it, seen live 2026-08-26), then shrinks the
// source with Item::SetStackSize + ControllerCharacter::SendUpdateItemStack. The replica copy is a plain byte
// copy: CreateItem only reads it (SetItemReplicaInfo deep-copies into the new item), nothing is constructed or
// freed on our side. The caller sells the clone by id a frame or two later (sell_split); a refused sale puts
// the clone into the bag, where the game merges it back into the stack (unsplit_stack).
static constexpr size_t kReplicaOff = 0x538, kReplicaSize = 0x190, kReplicaCountOff = 0x178;
unsigned split_stack(unsigned item_id, unsigned count) {
  void* e = engine(); void* c = controller();
  if (!e || !c || !g.Item_CreateItem || !g.Item_SetStackSize || !g.SendUpdateItemStack || !g.SendAddItemToInventory) return 0;
  void* item = object_by_id(item_id);
  unsigned orig = item ? item_stack(item) : 0;
  if (orig < 2 || count < 1 || count >= orig) { log::writef("gameapi: split_stack {}: {} of {} refused", item_id, count, orig); return 0; }
  unsigned cap = g.GetItemMaxStackSize ? g.GetItemMaxStackSize(e) : orig;
  if (count > cap) return 0;
  // Self-validating guard on the inline replica: SetItemReplicaInfo stamps its first dword with the object id.
  unsigned replica_id = 0;
  if (!read_mem((const char*)item + kReplicaOff, &replica_id, sizeof replica_id) || replica_id != item_id) {
    log::writef("gameapi: split_stack {}: replica id {} does not match (layout changed?)", item_id, replica_id);
    return 0;
  }
  alignas(8) unsigned char info[kReplicaSize];
  if (!read_mem((const char*)item + kReplicaOff, info, kReplicaSize)) return 0;
  *(unsigned*)(info + 0) = 0;
  *(unsigned*)(info + kReplicaCountOff) = count;
  unsigned new_id = 0;
  guarded("split stack", [&] {
    void* fresh = g.Item_CreateItem(info);
    if (!fresh) return;
    new_id = object_id(fresh);
    g.SendAddItemToInventory(c, new_id);
    g.Item_SetStackSize(item, orig - count);
    g.SendUpdateItemStack(c, item_id, orig - count);
  });
  invalidate_objects();
  log::writef("gameapi: split_stack {} x{} of {} -> clone {}", item_id, count, orig, new_id);
  return new_id;
}
// The clone of split_stack is not in the bag grid: only the sale request and the character-side removal.
bool sell_split(unsigned market_id, unsigned item_id) {
  void* e = engine(); void* c = controller();
  if (!e || !c || !g.PlayerSaleRequest || !g.SendRemoveItemFromInventory) return false;
  bool r = false;
  guarded("PlayerSaleRequest(split)", [&] {
    r = g.PlayerSaleRequest(e, market_id, item_id, false);
    if (r) g.SendRemoveItemFromInventory(c, item_id);
  });
  invalidate_objects();
  log::writef("gameapi: sell split {} to {} -> {}", item_id, market_id, r);
  return r;
}
bool unsplit_stack(unsigned item_id) {
  void* ic = inv_ctrl();
  if (!ic || !g.Inv_AddItem) return false;
  bool ok = false;
  guarded("unsplit", [&] { ok = g.Inv_AddItem(ic, item_id, true, false); });
  invalidate_objects();
  log::writef("gameapi: unsplit {} -> {}", item_id, ok);
  return ok;
}
// ---- the caravan ----
std::vector<Bag> stash_sacks() {
  std::vector<Bag> out;
  void* p = player(); void* e = engine(); load_items();
  if (p && g.GetPrivateStash) guarded("GetPrivateStash", [&] { std::vector<void*> sacks = vec_items<void*>(g.GetPrivateStash(p), 16); for (size_t i = 0; i < sacks.size(); ++i) out.push_back(read_sack(sacks[i], (int)i)); });
  if (e && g.GetPlayerTransfer) guarded("GetPlayerTransfer", [&] { std::vector<void*> sacks = vec_items<void*>(g.GetPlayerTransfer(e), 16); for (size_t i = 0; i < sacks.size(); ++i) out.push_back(read_sack(sacks[i], 100 + (int)i)); });
  return out;
}
// The stash grid's shift-click (exe+0x12f062): out of the stash, into the bag, and the controller told.
bool stash_to_bag(int sack_index, unsigned item_id) {
  void* p = player(); void* e = engine(); void* ic = inv_ctrl(); void* c = controller();
  if (!p || !e || !ic || !c || !g.Inv_AddItem) return false;
  bool ok = false;
  guarded("stash to bag", [&] {
    void* item = object_by_id(item_id);
    if (g.Inv_IsSpaceAvailable && item && !g.Inv_IsSpaceAvailable(ic, item)) return;
    bool removed = sack_index >= 100 ? (g.RemoveItemFromTransfer && g.RemoveItemFromTransfer(e, item_id)) : (g.RemoveItemFromPrivateStash && g.RemoveItemFromPrivateStash(p, item_id));
    if (!removed) return;
    if (!g.Inv_AddItem(ic, item_id, true, false)) return;
    if (g.SendAddItemToInventory) g.SendAddItemToInventory(c, item_id);
    ok = true;
  });
  invalidate_objects();
  log::writef("gameapi: stash sack {} item {} to bag ok={}", sack_index, item_id, ok);
  return ok;
}
// The bag's shift-click while the caravan is open (exe+0x1eaa65): into the selected stash / transfer sack.
bool bag_to_stash(unsigned item_id) {
  void* p = player(); void* e = engine(); void* ic = inv_ctrl(); void* c = controller();
  if (!p || !e || !ic || !c || !g.Inv_RemoveItem || !g.SendRemoveItemFromInventory) return false;
  bool ok = false;
  guarded("bag to stash", [&] {
    bool transfer = g.IsTransferOpen && g.IsTransferOpen(e);
    bool added = transfer ? (g.AddItemToTransfer && g.GetSelectedTransferSackNumber && g.AddItemToTransfer(e, item_id, g.GetSelectedTransferSackNumber(e), true))
                          : (g.AddItemToPrivateStash && g.GetSelectedStashSackNumber && g.AddItemToPrivateStash(p, g.GetSelectedStashSackNumber(p), item_id, true));
    if (!added) return;
    g.Inv_RemoveItem(ic, item_id, true);
    g.SendRemoveItemFromInventory(c, item_id);
    ok = true;
  });
  invalidate_objects();
  log::writef("gameapi: bag item {} to stash ok={}", item_id, ok);
  return ok;
}

std::string dump_bags() {
  std::string out = std::format("selected bag {} money {}\n", selected_bag(), money());
  for (const Bag& b : bags()) {
    out += std::format("bag {} '{}' {}x{} items={}{}\n", b.index, b.name, b.width, b.height, b.items.size(), b.debug);
    for (const BagItem& it : b.items) out += std::format("  id={} {} at ({:.0f},{:.0f}) {:.0f}x{:.0f} stack={} '{}' record={}\n", it.id, it.p, it.x, it.y, it.w, it.h, it.stack, it.name, object_record(it.p));
  }
  return out;
}
std::string dump_equipment() {
  std::string out = std::format("alternate={}\n", alternate_weapons());
  for (const EquipSlot& s : equipment()) out += std::format("  loc {:2} '{}': id={} {} '{}'\n", s.loc, s.label, s.item_id, s.item, s.name);
  return out;
}
}  // namespace gd::gameapi
