#include "screens/hotbar_manager.h"
#include <format>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "gameapi.h"
#include "screens/list_picker.h"
#include "screens/quickbar.h"
#include "screens/window_base.h"
#include "speech.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace { bool g_open = false; int g_swap_ticks = 0; bool g_swap_alt = false; }
void open_hotbar_manager() { g_open = true; }

// F: swap the active weapon set (only the two hands change). The controller applies the swap to the equipment
// view on a later tick, so the announce ("weapon set N, <right>, <left>") is deferred a few frames -- read the
// hands too early and they are still the old set's (measured 2026-08-23).
void swap_weapons() {
  g_swap_alt = gameapi::swap_weapon_set();
  g_swap_ticks = 3;
}
void weapon_swap_tick() {
  if (g_swap_ticks <= 0 || --g_swap_ticks > 0) return;
  MessageBuilder m;
  m.fragment(strings::kWeaponSet).fragment(std::format("{}", g_swap_alt ? 2 : 1));
  for (const gameapi::EquipSlot& s : gameapi::equipment())
    if (s.loc == 9 || s.loc == 10) m.list_item().fragment(s.name.empty() ? std::string(strings::kEmptySlot) : s.name);
  speech::speak(m.build(), true);
}

// Opens the skill picker for one hot-slot index: "clear" then every assignable skill (world::skill_aim filters
// out passives / modifiers / masteries). id 0 clears the slot (assign_skill_to_slot with 0 empties it).
static void open_slot_picker(unsigned index, const std::string& slot_label) {
  std::vector<PickerItem> items;
  items.push_back({0, std::string(strings::kClear), {}});
  // Only learned/available skills (level > 0): an unlearned skill won't stay on a slot (the game clears it),
  // and level-0 skills of masteries the player hasn't invested in are just noise. skill_aim != None drops
  // passives / modifiers / the mastery bar. Default utility skills (records/skills/default/* -- the basic
  // weapon attack, move-to, evade, the health/energy potions) are excluded: they have their own keys (mouse,
  // R/E) and only cluttered the list (they were the "Weapon Attack twice" and the potions the user saw).
  for (const gameapi::SkillInfo& s : gameapi::assignable_skills()) {
    if (!s.id || s.level == 0) continue;
    if (s.record.rfind("records/skills/default/", 0) == 0) continue;
    if (world::skill_aim(gameapi::object_by_id(s.id)) == world::SkillAim::None) continue;
    items.push_back({s.id, s.name, {}});
  }
  open_picker(slot_label, std::move(items), [index](unsigned id) {
    bool ok = gameapi::assign_skill_to_slot(index, id);
    speech::speak(ok ? std::string(id ? strings::kAssigned : strings::kCleared) : std::string(strings::kCannot), true);
  }, [](unsigned id, bool) { if (void* s = gameapi::object_by_id(id)) speak_lines(gameapi::skill_tooltip(s)); });   // Space = the skill's text
}

class HotbarManagerScreen : public Screen {
 public:
  std::string_view key() const override { return "hotbar_manager"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kHotbar); }
  // Just above the bare world, BELOW the game windows (inventory 11, vendor 14): opening a window covers the
  // manager, which then closes itself (below). Its own skill picker (layer 30) covers it without closing it.
  int layer() const override { return 1; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }
  void on_unfocus() override { if (!picker_open()) g_open = false; }   // covered by a game window (not our picker) -> close

  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    // The current weapon set's two bars (indices 0-9 and 14-23). The mouse buttons, R/E potions and Y-swap are
    // not listed -- they have their own keys and don't live on these pages (docs/controls.md).
    const std::vector<gameapi::HotSlot> all = gameapi::hotslots();
    for (int bar = 1; bar <= 2; ++bar)
      for (int k = 1; k <= 10; ++k) {
        unsigned idx = gameapi::quickbar_slot_index(bar - 1, k);
        std::string content = (idx < all.size() && !all[idx].empty) ? all[idx].name : std::string(strings::kEmptySlot);
        std::string label = std::format("{} {} {} {}", strings::kBar, bar, strings::kSlot, k % 10);
        b.add_item(ControlId::structural(std::format("hb.{}", idx)),
                   row_item(label, [content] { return content; }, [idx, label] { open_slot_picker(idx, label); }));
      }
  }
};

std::unique_ptr<Screen> make_hotbar_manager() { return std::make_unique<HotbarManagerScreen>(); }

}  // namespace gd::screens
