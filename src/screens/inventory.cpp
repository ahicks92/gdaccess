#include "screens/inventory.h"
#include <cstdlib>
#include <format>
#include <optional>
#include "app.h"
#include "core/navigator.h"
#include "gameapi.h"
#include "screens/list_picker.h"
#include "screens/skills.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;
namespace { int g_bag_source = 0; }
void set_bag_item_source(int s) { g_bag_source = s; }
int bag_item_source() { return g_bag_source; }

// The two weapon hands (EquipmentCtrlLocation): 9 = Right Hand (main), 10 = Left Hand (off-hand).
constexpr int kLocRightHand = 9, kLocLeftHand = 10;

class InventoryScreen : public WindowScreen, public AssignSource {
 public:
  InventoryScreen() : WindowScreen("inventory", std::string(strings::kInventory), exe_ui::ingame::kInventory, 11) {}

  // The equipment tab's weapon is an assign source: with a hand slot focused, the assign keys (Ctrl+J / Ctrl+I
  // -> left / right mouse, Ctrl+1..0 -> a slot) put the character's DEFAULT basic attack there. This is the
  // "get back to a basic attack" path -- the id comes from gameapi::default_skill_id (the game's own
  // GetDefaultSkillId), so it is always the right instance for the equipped weapon.
  unsigned focused_skill_id() override {
    if (tab() != 0) return 0;
    GraphNavigator* nav = app::navigator();
    std::optional<ControlId> id = nav ? nav->focused_id() : std::nullopt;
    if (!id || !id->structural_key().is_string()) return 0;
    const std::string& k = id->structural_key().text();
    if (k.rfind("inventory.eq", 0) != 0) return 0;
    int loc = std::atoi(k.c_str() + 12);
    if (loc != kLocRightHand && loc != kLocLeftHand) return 0;
    return gameapi::default_skill_id(0);
  }
  std::string focused_label() override { return focused_skill_id() ? std::string(strings::kBasicAttack) : std::string(); }

  void on_tab_changed(int index) override {
    int nbags = (int)bags_.value.size();
    if (index >= 1 && index <= nbags) gameapi::select_bag(index - 1);   // the game's window shows the same bag
    invalidate();
  }
  void on_focus() override { invalidate(); WindowScreen::on_focus(); }

  void build(GraphBuilder& b) override {
    const std::vector<gameapi::Bag>& bags = bags_.get([] { return gameapi::bags(); }, 30);
    std::vector<std::string> labels{std::string(strings::kEquipment)};
    for (const gameapi::Bag& bag : bags) labels.push_back(bag.name.empty() ? std::format("{} {}", strings::kBag, bag.index + 1) : bag.name);
    labels.push_back(std::string(strings::kStats));
    add_tabs(b, labels);
    int t = tab();
    if (t == 0) build_equipment(b);
    else if (t >= 1 && t <= (int)bags.size()) build_bag(b, bags[(size_t)t - 1]);
    else build_sheet(b);
  }

 private:
  void invalidate() { bags_.invalidate(); equipment_.invalidate(); sheet_.invalidate(); }
  void build_equipment(GraphBuilder& b) {
    const std::vector<gameapi::EquipSlot>& eq = equipment_.get([] { return gameapi::equipment(); }, 30);
    for (const gameapi::EquipSlot& s : eq) {
      std::string label = s.label.empty() ? std::format("slot {}", s.loc) : s.label;
      std::string name = s.name.empty() ? std::string(strings::kEmptySlot) : s.name;
      unsigned id = s.item_id; int loc = s.loc;
      // Enter opens the equip picker: everything across all bags that fits this slot (weapons/off-hands go to
      // the ACTIVE weapon set, since equip() uses the current EquipmentCtrl). Its first entry, "empty",
      // unequips. Backspace stays a direct unequip shortcut.
      auto open_equip = [this, loc, label] {
        std::vector<PickerItem> items;
        items.push_back({0, std::string(strings::kEmptySlot), {}});
        for (const gameapi::Bag& bag : gameapi::bags())
          for (const gameapi::BagItem& it : bag.items)
            if (gameapi::can_equip(it.id, loc))
              items.push_back({it.id, it.name.empty() ? std::format("item {}", it.id) : it.name, it.stack > 1 ? std::format("x{}", it.stack) : std::string()});
        open_picker(label, std::move(items), [this, loc](unsigned pid) {
          bool ok = pid ? gameapi::equip(pid, loc) : gameapi::unequip(loc);
          if (!ok) speech::speak(strings::kCannot, true);
          invalidate();
        });
      };
      auto unequip = [this, id, loc] { if (!id) { speech::speak(strings::kEmptySlot, true); return; } if (!gameapi::unequip(loc)) speech::speak(strings::kCannot, true); invalidate(); };
      b.add_item(ControlId::structural(std::format("inventory.eq{}", s.loc)),
                 row_item(label, [name] { return name; }, open_equip, id ? item_tip(id, false) : std::function<void()>{}, unequip, id ? item_tip(id, true) : std::function<void()>{}));
    }
    MessageBuilder m; strings::push_stat(m, strings::kIronBits, std::format("{}", gameapi::money()));
    b.add_item(ControlId::structural("inventory.money"), line_item(m.build()));
  }
  void build_bag(GraphBuilder& b, const gameapi::Bag& bag) {
    if (bag.items.empty()) { b.add_item(ControlId::structural(std::format("inventory.bag{}.empty", bag.index)), line_item(std::string(strings::kEmpty))); return; }
    for (const gameapi::BagItem& it : bag.items) {
      MessageBuilder m; strings::push_stack(m, it.name.empty() ? std::format("item {}", it.id) : it.name, it.stack);
      unsigned id = it.id;
      auto activate = [this, id] {
        void* p = gameapi::object_by_id(id);
        if (p && !gameapi::item_requirements_met(p)) { speech::speak(strings::kRequirementsNotMet, true); return; }
        gameapi::use_item(id, g_bag_source);
        invalidate();
      };
      b.add_item(ControlId::structural(std::format("inventory.item{}", it.id)), row_item(m.build(), {}, activate, item_tip(id, false), {}, item_tip(id, true)));
    }
  }
  void build_sheet(GraphBuilder& b) {
    const std::vector<gameapi::Stat>& rows = sheet_.get([] { return gameapi::character_sheet(); }, 30);
    int i = 0;
    for (const gameapi::Stat& s : rows) {
      MessageBuilder m; strings::push_stat(m, s.label, s.value);
      std::string id = std::format("inventory.stat{}", i++);
      if (s.spend) {   // Enter spends an attribute point here (the sheet's "+" button)
        int which = s.spend;
        auto spend = [this, which] {
          if (gameapi::attribute_points() == 0) { speech::speak(strings::kNoPoints, true); return; }
          speech::speak(gameapi::spend_attribute_point(which) ? std::string(strings::kPointSpent) : std::string(strings::kCannot), true);
          invalidate();
        };
        b.add_item(ControlId::structural(id), row_item(m.build(), {}, spend));
      } else {
        b.add_item(ControlId::structural(id), line_item(m.build()));
      }
    }
    if (rows.empty()) b.add_item(ControlId::structural("inventory.nostats"), line_item(std::string(strings::kEmpty)));
  }
  Snapshot<std::vector<gameapi::Bag>> bags_;
  Snapshot<std::vector<gameapi::EquipSlot>> equipment_;
  Snapshot<std::vector<gameapi::Stat>> sheet_;
};

std::unique_ptr<Screen> make_inventory() { return std::make_unique<InventoryScreen>(); }
}  // namespace gd::screens
