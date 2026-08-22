#include "screens/quickbar.h"
#include <format>
#include "app.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "gameapi.h"
#include "screens/skills.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;

// Hot-slot index of quickbar slot k (1..10) on the bar the HUD shows (bars start at 0, 14, 26, 36 -- the
// 2026-08-22 readout of the HUD buttons' slot indices).
unsigned quickbar_slot_index(int k) {
  int page = exe_ui::quickbar_page();
  return gameapi::quickbar_slot_index(page < 0 ? 0 : page, k);
}
void set_quickbar_base(unsigned, unsigned) {}   // the layout is known now; kept for the dev route

void speak_quickbar() {
  std::vector<gameapi::HotSlot> all = gameapi::hotslots();
  if (all.empty()) { speech::speak(strings::kNotInWorld, true); return; }
  MessageBuilder m;
  int page = exe_ui::quickbar_page();
  m.list_item().fragment(strings::kQuickbar).fragment(std::format("{}", (page < 0 ? 0 : page) + 1));
  for (int k = 1; k <= 10; ++k) {
    unsigned idx = quickbar_slot_index(k);
    const gameapi::HotSlot& s = all[idx < all.size() ? idx : 0];
    m.list_item().fragment(std::format("{}", k % 10)).fragment(s.empty ? std::string(strings::kEmptySlot) : s.name);
  }
  gameapi::HotSlot p = gameapi::primary_slot(), q = gameapi::secondary_slot();
  m.list_item().fragment(strings::kLeftMouse).fragment(p.empty ? std::string(strings::kEmptySlot) : p.name);
  m.list_item().fragment(strings::kRightMouse).fragment(q.empty ? std::string(strings::kEmptySlot) : q.name);
  speech::speak(m.build(), true);
}

void assign_focused(int target) {
  Screen* cur = app::screens().current();
  AssignSource* src = dynamic_cast<AssignSource*>(cur);
  unsigned skill = src ? src->focused_skill_id() : 0;
  if (!skill) { speech::speak(strings::kNothingToAssign, true); return; }
  std::string label = src->focused_label();
  bool ok = false;
  MessageBuilder m;
  if (target == 0) { ok = gameapi::set_primary_skill(skill); m.fragment(label).list_item().fragment(strings::kAssigned).fragment(strings::kLeftMouse); }
  else if (target < 0) { ok = gameapi::set_secondary_skill(skill); m.fragment(label).list_item().fragment(strings::kAssigned).fragment(strings::kRightMouse); }
  else { ok = gameapi::assign_skill_to_slot(quickbar_slot_index(target), skill); m.fragment(label).list_item().fragment(strings::kAssigned).fragment(strings::kSlot).fragment(std::format("{}", target % 10)); }
  speech::speak(ok ? m.build() : std::string(strings::kCannot), true);
}

// The game's own Pickup action (nearest item within 10 units, loot filter applied) through InGameUI::HandleKeyAction.
void pickup_nearest() {
  if (!exe_ui::ingame_key_action(0x37)) speech::speak(strings::kNothingToPickUp, true);
}
}  // namespace gd::screens
