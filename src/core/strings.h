#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "message_builder.h"

// The single home for mod-authored spoken strings and, more importantly, for the spoken CONVENTIONS:
// role words, how a position in a list reads, what "nothing here" sounds like. Game-provided text
// (item names, tooltips, menu labels) passes through verbatim; this is only the language we add
// around it. English only, by decision (2026-08-21). No inline literals in speech calls elsewhere.
namespace gd::strings {

// ---- roles (spoken after a control's label) ----
inline constexpr std::string_view kButton = "button";
inline constexpr std::string_view kToggle = "toggle";
inline constexpr std::string_view kSlider = "slider";
inline constexpr std::string_view kRadio = "radio button";
inline constexpr std::string_view kComboBox = "combo box";
inline constexpr std::string_view kTab = "tab";
inline constexpr std::string_view kTextField = "edit";
inline constexpr std::string_view kList = "list";
inline constexpr std::string_view kHeading = "heading";

// ---- states ----
inline constexpr std::string_view kSelected = "selected";
inline constexpr std::string_view kDisabled = "disabled";
inline constexpr std::string_view kOn = "on";
inline constexpr std::string_view kOff = "off";
inline constexpr std::string_view kNotSelected = "not selected";
inline constexpr std::string_view kEmpty = "empty";
inline constexpr std::string_view kEditing = "editing";  // a text field took the keyboard: type, Enter or Escape when done
inline constexpr std::string_view kNameNotTaken = "the game did not take the name";  // checkpoint after editing failed
inline constexpr std::string_view kSpace = "space";      // the typed-character echo for a space
inline constexpr std::string_view kExpanded = "expanded";
inline constexpr std::string_view kCollapsed = "collapsed";

// ---- feedback ----
inline constexpr std::string_view kNoTooltip = "no tooltip";
inline constexpr std::string_view kNothingToCompare = "nothing to compare";
inline constexpr std::string_view kNoDetails = "no details";
inline constexpr std::string_view kNoMatch = "no match for";
inline constexpr std::string_view kSearchCleared = "search cleared";
inline constexpr std::string_view kNothingThere = "nothing there";
inline constexpr std::string_view kNoTextOnScreen = "no text on screen";
inline constexpr std::string_view kUnsupportedScreen = "unsupported screen";
inline constexpr std::string_view kModLoaded = "G D Access loaded";
inline constexpr std::string_view kModLoadedNoSpeech = "G D Access loaded, no speech backend";

// ---- composed shapes: push_* helpers so call sites never concatenate ----
// "3 of 7"
gd::core::MessageBuilder& push_position(gd::core::MessageBuilder& m, int index1, int count);
// "<label>, <role>[, selected][, disabled]" as list items
gd::core::MessageBuilder& push_control(gd::core::MessageBuilder& m, std::string_view label, std::string_view role, bool selected, bool disabled);
// "<n> items" / "1 item"
gd::core::MessageBuilder& push_count(gd::core::MessageBuilder& m, int count, std::string_view singular, std::string_view plural);
// "x 59, z 97, life 250 of 250, region <name>" -- the in-game "where am I" readout.
gd::core::MessageBuilder& push_where(gd::core::MessageBuilder& m, float x, float z, std::string_view region, double life, float life_max);

// ---- in-game ----
// "Hangman Jarvis, 5 away, 2 o'clock, 1 of 3" -- a review-cursor landing (game label verbatim).
gd::core::MessageBuilder& push_scan_item(gd::core::MessageBuilder& m, std::string_view label, float distance, int clock_hour, int index1, int count, bool distant, std::string_view note = {});
// "level 3 Arcanist, hardcore" -- a main-menu character row's value (class empty = no mastery yet).
// "Hellhound down" / "Hellhound summoned" -- a pet event line (game label verbatim + our word).
gd::core::MessageBuilder& push_pet_event(gd::core::MessageBuilder& m, std::string_view label, std::string_view event);
gd::core::MessageBuilder& push_character_summary(gd::core::MessageBuilder& m, unsigned level, std::string_view class_name, bool hardcore);
// The MonsterClassification rarity word (0 Common -> "", 1 champion, 2 hero, 3 boss, 4 quest, 5 super boss).
std::string_view classification_word(int classification);
// "walking undead level 5 hero" -- an enemy review label (the game name, its level, its rarity word if any).
gd::core::MessageBuilder& push_enemy_label(gd::core::MessageBuilder& m, std::string_view name, int level, int classification);
// "100 percent health, frozen, stunned" -- the / inspect readout of the current target (no name repeat).
gd::core::MessageBuilder& push_target_inspect(gd::core::MessageBuilder& m, int health_percent, const std::vector<std::string>& effects);
// "5 away, 2 o'clock" -- the distance and bearing part alone (a riftgate row's value).
gd::core::MessageBuilder& push_distance_bearing(gd::core::MessageBuilder& m, float distance, int clock_hour);
// "no enemies nearby"
gd::core::MessageBuilder& push_nothing_nearby(gd::core::MessageBuilder& m, std::string_view group_plural);
// "<speaker>: <speech>" -- a conversation node (speaker may be empty).
gd::core::MessageBuilder& push_speech(gd::core::MessageBuilder& m, std::string_view speaker, std::string_view speech);
inline constexpr std::string_view kEnemies = "enemies";
inline constexpr std::string_view kUnknown = "unknown";
inline constexpr std::string_view kSecondaryBag = "secondary";   // the game's selected bag: where pickups overflow to once bag 1 is full (bag 1 is always first)
inline constexpr std::string_view kNotABag = "not a bag";
inline constexpr std::string_view kOffering = "offering";   // a shrine's required item row: "offering 1, Aether Crystal"
inline constexpr std::string_view kPets = "pets";                 // the [ / ] review group and the pet overlay
inline constexpr std::string_view kStanceNormal = "normal";       // Monster::ControllerType 0 / 1 / 2 (docs/pets.md)
inline constexpr std::string_view kStanceAggressive = "aggressive";
inline constexpr std::string_view kStanceDefensive = "defensive";
inline constexpr std::string_view kPetDown = "down";              // "<pet> down" (Zira) when a pet leaves the list
inline constexpr std::string_view kPetSummoned = "summoned";      // "<pet> summoned"
inline constexpr std::string_view kDeselected = "deselected";
inline constexpr std::string_view kAllPets = "all pets";
inline constexpr std::string_view kNoPets = "no pets";
inline constexpr std::string_view kDisbanded = "disbanded";
inline constexpr std::string_view kPetsAttack = "attack locked target";   // overlay command rows
inline constexpr std::string_view kPetsRecall = "recall";
inline constexpr std::string_view kSelectedPets = "selected pets";
inline constexpr std::string_view kNeutrals = "people and objects";
inline constexpr std::string_view kBystanders = "bystanders";
inline constexpr std::string_view kLoot = "loot";
inline constexpr std::string_view kLootFilter = "loot filter";
inline constexpr std::string_view kShowingAllItems = "showing all items";   // O: the review groups and the sonar ignore the loot filter (and the game shows every label)
inline constexpr std::string_view kLootFilterOn = "loot filter on";
inline constexpr std::string_view kSetToDefaults = "set to defaults";       // the last row of each loot filter column
inline constexpr std::string_view kDefaults = "defaults";
inline constexpr std::string_view kSonarOn = "sonar on";
inline constexpr std::string_view kHardcore = "hardcore";
inline constexpr std::string_view kNoted = "noted";
inline constexpr std::string_view kNoteUseHint = "Enter reads it into the codex";
inline constexpr std::string_view kNoteFailed = "could not write the note";
inline constexpr std::string_view kSonarOff = "sonar off";
inline constexpr std::string_view kTransitions = "dungeon entrances";
inline constexpr std::string_view kNoTarget = "no target";
inline constexpr std::string_view kInGame = "in game";
inline constexpr std::string_view kMessage = "message";  // the game's generic message box
inline constexpr std::string_view kPauseMenu = "pause menu";
inline constexpr std::string_view kLoading = "loading";
inline constexpr std::string_view kTip = "tip";      // a tutorial tip, shown in our own overlay
inline constexpr std::string_view kConversation = "conversation";
inline constexpr std::string_view kClose = "close";
inline constexpr std::string_view kNotInWorld = "not in the world";
// rooms (docs/rooms.md): the place announcement, the X description, the V exit cycle
inline constexpr std::string_view kNoRoom = "no room data here";
inline constexpr std::string_view kNoDescription = "no description yet";
inline constexpr std::string_view kExits = "exits";               // the review group's plural ("no exits nearby")
inline constexpr std::string_view kBlocked = "blocked";            // the exit item's note (push_scan_item)
inline constexpr std::string_view kRoom = "room";                  // untitled room: "room 12"
// "Devil's Crossing, the prison, cell block corridor" -- only the parts that changed, in that order
gd::core::MessageBuilder& push_place(gd::core::MessageBuilder& m, std::string_view region, std::string_view subregion, std::string_view room);
inline constexpr std::string_view kOptions = "options";       // the main menu's unlabeled icon buttons
inline constexpr std::string_view kExitGame = "exit game";
inline constexpr std::string_view kYes = "yes";               // the game's message-box answers (answered through its DialogManager)
inline constexpr std::string_view kNo = "no";
inline constexpr std::string_view kOkay = "okay";
inline constexpr std::string_view kConfirmation = "confirmation";  // Delete Character's type-DELETE box
inline constexpr std::string_view kOptionsScreen = "Options";
inline constexpr std::string_view kTooFarAway = "too far away";  // a click on a reviewed thing the camera does not show
inline constexpr std::string_view kDistant = "distant";          // the same thing while cycling through the review list
inline constexpr std::string_view kPercent = "percent";
inline constexpr std::string_view kKeyBindings = "key bindings";
// ---- combat (spoken through the positional voices: Mark at the enemy, Zira for the player) ----
inline constexpr std::string_view kHealth = "health";
inline constexpr std::string_view kEnergy = "energy";
inline constexpr std::string_view kCrit = "crit";
inline constexpr std::string_view kVoiceUnavailable = "combat speech is unavailable; using the screen reader";
// "health 250 of 250, energy 100 of 100" (the H key)
gd::core::MessageBuilder& push_vitals(gd::core::MessageBuilder& m, double life, float life_max, float energy, float energy_max);
// "health 70 percent" -- a 10 % step crossed
gd::core::MessageBuilder& push_health_percent(gd::core::MessageBuilder& m, int percent);
// "456" / "456 crit" -- the number the game drew over the enemy
gd::core::MessageBuilder& push_combat_hit(gd::core::MessageBuilder& m, std::string_view number, bool crit);
// "Miss" / "Dodge" / "Block" -- game text, verbatim
gd::core::MessageBuilder& push_combat_word(gd::core::MessageBuilder& m, std::string_view word);
inline constexpr std::string_view kExp = "exp";       // deliberately terse (the game shows XP only as a filling bar)
inline constexpr std::string_view kKilled = "killed";
// Kill feedback, coalesced per window (the game shows an enemy dying only graphically), spoken in Zira. A single
// kill is just the XP ("300 exp", "0 exp" when none). A pack is "N killed", plus ", M exp" when it yielded XP.
gd::core::MessageBuilder& push_kills(gd::core::MessageBuilder& m, int count, uint64_t xp);
// ---- the in-world windows (src/screens/codex.cpp, factions.cpp, inventory.cpp, skills.cpp, quickbar) ----
inline constexpr std::string_view kCodex = "codex";
inline constexpr std::string_view kQuests = "quests";
inline constexpr std::string_view kCompletedQuests = "completed quests";
inline constexpr std::string_view kLore = "lore";
inline constexpr std::string_view kTracked = "tracked";
inline constexpr std::string_view kNotTracked = "not tracked";
inline constexpr std::string_view kDone = "done";
inline constexpr std::string_view kNoQuests = "no quests";
inline constexpr std::string_view kNoObjectives = "no objectives";
inline constexpr std::string_view kObjectives = "objectives";
inline constexpr std::string_view kReward = "reward";
inline constexpr std::string_view kFactions = "factions";
inline constexpr std::string_view kRiftgates = "riftgate travel";
inline constexpr std::string_view kNoRiftgates = "no riftgates discovered";
inline constexpr std::string_view kYouAreHere = "you are here";
// The map-marker picker (Ctrl+M) and the follow key (').
inline constexpr std::string_view kMapMarkers = "map";
inline constexpr std::string_view kQuestMarkers = "quest markers";
inline constexpr std::string_view kMapPoints = "points of interest";
inline constexpr std::string_view kNoMarkersHere = "nothing on the map here";
inline constexpr std::string_view kNoQuestMarkers = "no quest markers";
inline constexpr std::string_view kFollowing = "following";
inline constexpr std::string_view kNotFollowing = "not following anything";
inline constexpr std::string_view kNoFactions = "no factions known";
inline constexpr std::string_view kInventory = "inventory";
inline constexpr std::string_view kEquipment = "equipment";
inline constexpr std::string_view kBag = "bag";
inline constexpr std::string_view kStats = "stats";
inline constexpr std::string_view kEmptySlot = "empty";
inline constexpr std::string_view kWithComponent = "with component";   // the bag tile's component badge, on the row label
inline constexpr std::string_view kNothingEquipped = "nothing equipped";   // Backslash on an item: the slot it fits holds nothing
inline constexpr std::string_view kNotEquipment = "not equipment";         // Backslash on a potion / component / note
inline constexpr std::string_view kIronBits = "iron bits";
inline constexpr std::string_view kSkills = "skills";
inline constexpr std::string_view kSkillPointsLeft = "skill points";
inline constexpr std::string_view kLocked = "locked";
inline constexpr std::string_view kMastery = "mastery";
inline constexpr std::string_view kQuickbar = "quickbar";
inline constexpr std::string_view kHotbar = "hotbar";        // the hotbar manager screen
inline constexpr std::string_view kBar = "bar";
inline constexpr std::string_view kClear = "clear";          // the picker's clear-this-slot entry
inline constexpr std::string_view kCleared = "cleared";
inline constexpr std::string_view kDefault = "default";      // the mouse picker's reset-to-basic-attack entry
inline constexpr std::string_view kHealthPotion = "health potion";
inline constexpr std::string_view kEnergyPotion = "energy potion";
inline constexpr std::string_view kAssignSkill = "assign skill";  // hotbar-manager picker title
inline constexpr std::string_view kWeaponSet = "weapon set";
inline constexpr std::string_view kSlot = "slot";
inline constexpr std::string_view kLeftMouse = "left mouse";
inline constexpr std::string_view kRightMouse = "right mouse";
// How a slotted skill aims (appended to the slot readout; docs/skills-targeting.md).
inline constexpr std::string_view kAimSelf = "self";
inline constexpr std::string_view kAimAround = "around you";
inline constexpr std::string_view kAimPoint = "at a spot";
inline constexpr std::string_view kAimTarget = "at a target";
inline constexpr std::string_view kAssigned = "assigned";
inline constexpr std::string_view kBasicAttack = "basic attack";   // the weapon's default attack (equipment tab -> mouse)
inline constexpr std::string_view kNothingToAssign = "nothing to assign";
inline constexpr std::string_view kCannot = "can't";
inline constexpr std::string_view kClass = "class";
inline constexpr std::string_view kLevel = "level";
inline constexpr std::string_view kExperience = "experience";
inline constexpr std::string_view kAttributePoints = "attribute points";
inline constexpr std::string_view kSkillPoints = "skill points";
inline constexpr std::string_view kDevotionPoints = "devotion points";
inline constexpr std::string_view kOffensiveAbility = "offensive ability";
inline constexpr std::string_view kDefensiveAbility = "defensive ability";
inline constexpr std::string_view kDps = "damage per second";
inline constexpr std::string_view kRequirementsNotMet = "requirements not met";
inline constexpr std::string_view kAttach = "attach";
inline constexpr std::string_view kComponent = "component";
inline constexpr std::string_view kNoCompatibleItems = "no compatible items";
inline constexpr std::string_view kNoClass = "no class";
inline constexpr std::string_view kModifier = "modifier";
inline constexpr std::string_view kRequiresMastery = "needs mastery";
inline constexpr std::string_view kNothingToPickUp = "nothing to pick up";
inline constexpr std::string_view kQuestReward = "quest reward";
inline constexpr std::string_view kAccept = "accept";
inline constexpr std::string_view kShrine = "shrine";
inline constexpr std::string_view kOffer = "offer";
inline constexpr std::string_view kVendor = "vendor";
inline constexpr std::string_view kBuy = "buy";
inline constexpr std::string_view kSell = "sell";
inline constexpr std::string_view kBought = "bought";
inline constexpr std::string_view kSold = "sold";
inline constexpr std::string_view kSellHowMany = "sell how many of";   // "sell how many of 12" -- the partial-sell count prompt
inline constexpr std::string_view kNotAStack = "not a stack";
inline constexpr std::string_view kStash = "stash";
inline constexpr std::string_view kTransfer = "transfer";
inline constexpr std::string_view kMoved = "moved";
inline constexpr std::string_view kDetailsHint = "Ctrl+Space for details";   // appended to a short tooltip whose detailed form says more
inline constexpr std::string_view kSelectClass = "select class";
inline constexpr std::string_view kUndoClassSelection = "undo class selection";
inline constexpr std::string_view kClassChosen = "chosen; spend a point on the mastery to make it permanent";
inline constexpr std::string_view kSecondClassAt = "second class available at level";
inline constexpr std::string_view kPointSpent = "point spent";
inline constexpr std::string_view kNoPoints = "no points";
inline constexpr std::string_view kAtMaximum = "at maximum";
inline constexpr std::string_view kRequires = "requires";        // "requires <base skill>" for a modifier's prerequisite
inline constexpr std::string_view kModifies = "modifies";        // "modifies <base skill>" for a modifier skill
// Spirit-guide reclamation (the skills window opened in reclaim mode): a non-interactive hint row at the top of
// the skill list, and each skill's reclaim cost. The game's own word is "reclaim".
inline constexpr std::string_view kSpiritGuide = "spirit guide";
inline constexpr std::string_view kReclaimHint = "Backspace to reclaim a skill point";
inline constexpr std::string_view kToReclaim = "to reclaim";     // "<N> iron bits to reclaim" on each skill row
inline constexpr std::string_view kEach = "each";
inline constexpr std::string_view kReclaimed = "reclaimed";
inline constexpr std::string_view kNotEnoughBits = "not enough iron bits";
inline constexpr std::string_view kNothingToReclaim = "nothing to reclaim";
inline constexpr std::string_view kUndoPoints = "undo points";   // the skills window's own button: revert the points spent since it opened
// Devotion (the skills window's Constellations / Celestial Powers tabs; docs/devotion.md)
inline constexpr std::string_view kConstellations = "constellations";
inline constexpr std::string_view kCelestialPowers = "celestial powers";
inline constexpr std::string_view kCelestialPower = "celestial power";
inline constexpr std::string_view kAffinities = "affinities";
inline constexpr std::string_view kNoAffinity = "no affinity";
inline constexpr std::string_view kAvailable = "available";
inline constexpr std::string_view kStar = "star";                  // "star 3" -- stars have no names of their own
inline constexpr std::string_view kNeedsStar = "needs star";       // "needs star 2": the linked star is not learned yet
inline constexpr std::string_view kNeeds = "needs";                // "needs Chaos 4": the constellation's affinity requirement
inline constexpr std::string_view kGives = "gives";                // "gives Chaos 3, Eldritch 2": the completion bonus
inline constexpr std::string_view kLearned = "learned";
inline constexpr std::string_view kComplete = "complete";
inline constexpr std::string_view kConstellationComplete = "constellation complete";
inline constexpr std::string_view kAttachedTo = "attached to";     // a celestial power's host skill
inline constexpr std::string_view kNotAttached = "not attached";
inline constexpr std::string_view kHas = "has";                    // "<skill>, has <power>" in the host picker
inline constexpr std::string_view kFrom = "from";                  // "from Bat": a power's constellation
inline constexpr std::string_view kAssign = "assign";              // the host picker's title: "assign Twin Fangs to"
inline constexpr std::string_view kTo = "to";
inline constexpr std::string_view kHave = "have";                  // "requires Eldritch 1, have 4"
inline constexpr std::string_view kNone = "none";
inline constexpr std::string_view kReplaced = "replaced";
inline constexpr std::string_view kNoCelestialPowers = "no celestial powers learned";
inline constexpr std::string_view kNeededByStar = "needed by star";      // "needed by star 3": a learned star hangs off this one
inline constexpr std::string_view kWouldLock = "would lock";             // "would lock Raven": losing the bonus drops that constellation below its requirement
inline constexpr std::string_view kAetherCrystals = "aether crystals";
inline constexpr std::string_view kNotEnoughAether = "not enough aether crystals";
inline constexpr std::string_view kReclaimDevotionHint = "Backspace to reclaim a devotion point";
inline constexpr std::string_view kAnd = "and";
inline constexpr std::string_view kLost = "lost";                       // "constellation complete lost": the bonus went with the star
// "Waking to Misery: Enter the Cave under Burial Hill" -- one open objective of a tracked quest
gd::core::MessageBuilder& push_quest_objective(gd::core::MessageBuilder& m, std::string_view quest, std::string_view objective);
// "enter 1 to 12" -- a count prompt refusing an out-of-range value
gd::core::MessageBuilder& push_range_hint(gd::core::MessageBuilder& m, unsigned lo, unsigned hi);
// "<name>, x 3" -- a stacked item
gd::core::MessageBuilder& push_stack(gd::core::MessageBuilder& m, std::string_view name, unsigned stack);
// "<label>: <value>" -- a sheet row
gd::core::MessageBuilder& push_stat(gd::core::MessageBuilder& m, std::string_view label, std::string_view value);
// "<name>, level 3 of 12" -- a skill row
gd::core::MessageBuilder& push_skill_level(gd::core::MessageBuilder& m, unsigned level, unsigned max_level);
// "<name>, <level name>, 1500 of 5000" -- a faction row
gd::core::MessageBuilder& push_faction(gd::core::MessageBuilder& m, std::string_view name, std::string_view level_name, float value, int low, int high);

inline constexpr std::string_view kUnsupportedGameVersion = "this game version is not supported by G D Access; menus will not be read";

}  // namespace gd::strings
