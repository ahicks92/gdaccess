#pragma once
// The in-world windows' MODEL, read and driven through Game.dll / Engine.dll exports (static survey in
// docs/ingame-ui-survey.md): quests, factions, the quickbar, the lore codex, bags and equipment, skills, the
// character sheet. Screens (src/screens/*) present these; nothing here reads drawn text or widget state.
// Every function is game-thread only and returns copies (no game pointer is held across frames except the
// opaque object pointers the screens re-validate through object_by_id on use). Faults inside the game's
// calls are caught (SEH) and logged; the call then reports empty / false.
#include <cstdint>
#include <string>
#include <vector>

namespace gd::gameapi {
void load();                  // resolve the exports (once); missing ones are logged and their features report empty
void* engine();               // *gGameEngine (exported data); null before the world
void* player();               // the main player (Character / Player)
void* controller();           // the main player's ControllerPlayer (captured by world.cpp)
// A localization tag -> the game's text (LocalizationManager::LocalizeWithoutParams), colour codes stripped;
// empty when the tag is unknown.
std::string localize(const std::string& tag);

// ---- objects by id (inventory items, notes, skills come back as ids) ----
void* object_by_id(unsigned id);   // null when no such object; the lookup is a cached ObjectManager sweep
void invalidate_objects();         // after an action that may have created/destroyed objects
unsigned object_id(const void* object);
std::string object_record(const void* object);   // Object::GetObjectName: the .dbr path

// ---- objectives / quests ----
std::vector<std::string> objectives();           // the HUD's objective tracker lines
struct Objective { std::string text; int satisfied; };   // satisfied: the game's Quest2Objective::Satisfied enum, raw
struct Task { void* p; std::string name, description; int state; std::vector<Objective> objectives; std::vector<std::string> rewards; };  // state 1 available, 2 in progress, 3 complete
struct Quest { void* p; unsigned id; std::string name, group; bool tracked, complete, in_progress; std::vector<Task> tasks; };
enum QuestFilter : int { kQuestsAll = 0, kQuestsInProgress = 1, kQuestsCompleted = 2, kQuestsTracked = 4 };
std::vector<Quest> quests(int filter);
bool set_quest_tracked(void* quest, bool on);

// ---- factions ----
struct Faction { int type; std::string tag, name, level_name; float value; int level, low, high; bool unlocked; };
std::vector<Faction> factions();                 // the player-visible factions in the game's enum order

// ---- the quickbar (hot slots) ----
struct HotSlot { unsigned index; std::string name; int type; unsigned skill_id; int cooldown_ms; int status; bool empty; };
constexpr unsigned kHotSlotCount = 47;
std::vector<HotSlot> hotslots();                 // every slot of the displayed set (index = the game's slot index)
HotSlot hotslot(unsigned index);
HotSlot primary_slot();                          // left mouse
HotSlot secondary_slot();                        // right mouse
HotSlot health_potion_slot();                    // the R / health potion slot (read-only; the game auto-manages it)
HotSlot mana_potion_slot();                      // the E / energy potion slot (read-only)
std::vector<std::string> hotslot_tooltip(unsigned index);
bool assign_skill_to_slot(unsigned index, unsigned skill_id);   // a HotSlotOptionSkill built here (the game copies it)
bool set_primary_skill(unsigned skill_id);
bool set_secondary_skill(unsigned skill_id);
bool activate_hotslot(unsigned index);

// ---- the lore codex ----
struct Note { unsigned id; void* p; std::string title, heading; };
std::vector<Note> lore_notes();
// The FULL localized note text (the record's itemText tag), split into paragraphs; empty when the object has
// no text tag. The game's own tooltip truncates long notes; the reader uses this tag.
std::vector<std::string> note_full_text(void* note);
std::vector<std::string> note_text(void* note_item);   // ItemNote::GetUIDisplayText, one string per line

// ---- bags and equipment ----
struct BagItem { unsigned id; void* p; float x, y, w, h; std::string name; unsigned stack; };
struct Bag { int index; std::string name; unsigned width, height; std::vector<BagItem> items; std::string debug; };   // items in reading order (rows, then columns)
std::vector<Bag> bags();
int selected_bag();
bool select_bag(int index);
struct EquipSlot { int loc; std::string label; unsigned item_id; void* item; std::string name; };   // loc = EquipmentCtrlLocation 1..14
std::vector<EquipSlot> equipment();
bool alternate_weapons();                        // the weapon-swap set in use (EquipmentCtrl::GetIsAlternate)
bool can_equip(unsigned item_id, int loc);       // EquipmentCtrl::CanItemBePlaced -- the equip picker's filter
bool swap_weapon_set();                          // toggle the active weapon set (the two hands); returns new state (true = alternate)
unsigned money();                                // iron bits
bool dev_add_money(unsigned bits);               // dev only: Character::AddMoney
std::string item_name(const void* item);         // Item::GetGameDescription (virtual)
unsigned item_stack(const void* item);
std::vector<std::string> item_tooltip(const void* item, bool simple, bool details = false);   // Item::GetUIDisplayText(details = the Ctrl-held form) / GetSimpleUIDisplayText, virtual
bool item_requirements_met(const void* item);
// Actions (each is the game's own call; the screens re-snapshot afterwards).
bool use_item(unsigned id, int source);          // PlayerInventoryCtrl::UseItem (the bag's right-click: equip / drink / read)
bool drop_item(unsigned id);                     // ControllerCharacter::SendDropItemRandom
bool unequip(int loc);                           // EquipmentCtrl::RemoveItem on the slot's item
bool equip(unsigned id, int loc);                // EquipmentCtrl::PlaceItem(loc, id, ...)
bool pickup_item(unsigned id);                   // ControllerCharacter::PickupItem (the game's pickup command; no range check)

// ---- merchants and the caravan ----
struct MarketTab { int type; std::string name; std::vector<BagItem> items; };   // one per Market_TypeEnum the merchant stocks (probed 0..7)
std::vector<MarketTab> market_stock(unsigned market_id);
std::string market_price_text(unsigned market_id, unsigned item_id, bool buying);   // CreateUIPlayerBuyText / SellText, joined
bool buy(unsigned market_id, unsigned item_id);          // GameEngine::PlayerPurchaseRequest
bool sell(unsigned market_id, unsigned item_id);         // the bag's right-click-to-sell: PlayerSaleRequest + bag removal
std::string vendor_dump(unsigned market_id);              // dev: market map keys + market_stock(id) probe
std::vector<Bag> stash_sacks();                          // private stash sacks (index 0..) then transfer sacks (index 100..)
bool stash_to_bag(int sack_index, unsigned item_id);     // the stash grid's shift-click: to the bag
bool bag_to_stash(unsigned item_id);                     // the bag's shift-click while the caravan is open
// Quickbar layout (47 slots per weapon config): bars 1..4 start at 0, 14, 26, 36; left mouse 10 (config A) / 11
// (config B); right mouse 12 / 13; health potion 24; energy potion 25; evade 46.
unsigned quickbar_slot_index(int bar, int k);    // bar 0..3, k 1..10

// ---- skills ----
struct SkillInfo {
  void* p; unsigned id; std::string name, record; unsigned level, max_level, ultimate_level, mastery_id, mastery_req, tier;
  bool locked, is_mastery, enabled, modifier;
  bool item_auto = false;   // Skill::IsItemSkillAuto: an auto-triggered item skill (a proc / chance-on-attack) -- not player-assignable
  unsigned mastery_level = 0;       // Skill::GetMasteryLevel: this skill's mastery bar level (for the requirement gate)
  unsigned modified_skill_id = 0;   // Skill::GetModifiedSkillId: the base skill a modifier enhances (0 = not a modifier)
};
std::vector<SkillInfo> skills();                 // the UI skill list, in the game's order
unsigned skill_points();
unsigned default_skill_id(int role);             // SkillManager::GetDefaultSkillId (0 = left mouse basic attack, 1 = right); live, never cache
std::vector<unsigned> item_skill_ids();          // skills granted by equipped items (SkillManager::GetItemSkillList)
std::vector<SkillInfo> assignable_skills();      // UI skills + EQUIPPED-item granted skills (deduped); caller filters by skill_aim
std::string dump_item_skills();                  // dev: GetItemSkillList vs equipped-item granted skills
unsigned masteries_allowed();
std::vector<unsigned> mastery_ids();             // the masteries the character has
std::vector<std::string> skill_tooltip(const void* skill);   // GameEngine::GenerateUISkillText
// "" = the skill can take a point now; otherwise a human reason (no points / mastery rank / base skill).
std::string can_learn_skill(const void* skill);
bool learn_skill(const void* skill);             // +1 level (a skill point); refuses unless can_learn_skill is ""
std::string can_reclaim_skill(const void* skill); // "" = can reclaim now; else the reason (mastery last point / not enough bits)
bool refund_skill(const void* skill);            // -1 level; only at a spirit guide (exe_ui::skills_reclaim_mode)
unsigned reclaim_cost();                          // SkillManager::GetCurrentSkillReclamationCost (iron bits for the next reclaim)

// Masteries offered for selection: the six base classes (tagSkillClassName01..06 / tagSkillClassDescription01..06);
// `enumeration` is the game's mastery index (0 = Soldier), also the pane index for exe_ui::skills_set_pane.
struct MasteryChoice { int enumeration; std::string name, description; };
std::vector<MasteryChoice> mastery_choices();
// The mastery skill (the "class training" skill) of a mastery enumeration, or null.
const SkillInfo* mastery_skill(const std::vector<SkillInfo>& list, int enumeration);

// ---- the character sheet ----
struct Stat { std::string label, value; int spend = 0; std::string desc; };   // spend 1..3 = the row takes an attribute point (Physique / Cunning / Spirit); desc = the game's tooltip (Space)
std::vector<Stat> character_sheet();
// The attribute "+" buttons (ControllerCharacter::IncrementCharacter*, + the life/energy increments, as the
// sheet's own handler does): which = 1 Physique, 2 Cunning, 3 Spirit. False when no points are left.
bool spend_attribute_point(int which);
unsigned attribute_points();

// ---- Lua (dev): run a chunk in the game's LuaJIT state (LuaManager::RunCode on *(gEngine+0x68)) ----
bool lua_run(const std::string& code);

// ---- dev dumps ----
std::string dump_quests(int filter);
std::string dump_factions();
std::string dump_hotslots();
std::string dump_lore();
std::string dump_bags();
std::string dump_equipment();
std::string dump_skills();
bool dev_add_experience(unsigned xp);   // dev only: SkillManager::AddExperience on the main player
bool dev_open_skill_reclaim();          // dev only: open the skills window in spirit-guide reclaim mode
std::string dump_sheet();
std::string dump_object(unsigned id);
std::string dump_objects_stats();
}  // namespace gd::gameapi
