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
#include "world.h"

namespace gd::screens {
using namespace gd::core;

// Hot-slot index of quickbar slot k (1..10) on the bar the HUD shows (bars start at 0, 14, 26, 36 -- the
// 2026-08-22 readout of the HUD buttons' slot indices).
unsigned quickbar_slot_index(int k) {
  int page = exe_ui::quickbar_page();
  return gameapi::quickbar_slot_index(page < 0 ? 0 : page, k);
}
void set_quickbar_base(unsigned, unsigned) {}   // the layout is known now; kept for the dev route

namespace {
// How a slotted skill aims (docs/skills-targeting.md), appended after the skill name so you know before you
// fire it. Empty for a passive/modifier, a potion, or an empty slot.
std::string_view aim_word(world::SkillAim a) {
  switch (a) {
    case world::SkillAim::SelfCast: return strings::kAimSelf;
    case world::SkillAim::AroundYou: return strings::kAimAround;
    case world::SkillAim::AtPoint: return strings::kAimPoint;
    case world::SkillAim::AtTarget: return strings::kAimTarget;
    default: return {};
  }
}
// Speak one slot: "<lead> <name>, <aim>", or "<lead> empty".
void speak_slot_line(std::string_view lead, const gameapi::HotSlot& s) {
  MessageBuilder m;
  if (s.empty) { m.list_item().fragment(lead).fragment(strings::kEmptySlot); speech::speak(m.build(), true); return; }
  m.list_item().fragment(lead).fragment(s.name);
  std::string_view aim = s.skill_id ? aim_word(world::skill_aim(gameapi::object_by_id(s.skill_id))) : std::string_view{};
  if (!aim.empty()) m.list_item().fragment(aim);
  speech::speak(m.build(), true);
}
}  // namespace

void speak_slot(int k) {
  if (!world::in_world()) { speech::speak(strings::kNotInWorld, true); return; }
  speak_slot_line(std::format("{}", k % 10), gameapi::hotslot(quickbar_slot_index(k)));
}
void speak_mouse(bool primary) {
  if (!world::in_world()) { speech::speak(strings::kNotInWorld, true); return; }
  speak_slot_line(primary ? strings::kLeftMouse : strings::kRightMouse, primary ? gameapi::primary_slot() : gameapi::secondary_slot());
}

// Announce the displayed quickbar page when it changes (the game's own Y = Quickbar Switch cycles it; we only
// add the readout). Called every world frame; the first observation seeds without speaking, and re-entering
// the world resets so the next switch announces afresh.
namespace { int g_last_page = -2; }
void quickbar_tick() {
  int p = exe_ui::quickbar_page();
  if (p < 0) { g_last_page = -2; return; }
  if (g_last_page != -2 && p != g_last_page) {
    MessageBuilder m; m.fragment(strings::kQuickbar).fragment(std::format("{}", p + 1));
    speech::speak(m.build(), true);
  }
  g_last_page = p;
}
void quickbar_reset() { g_last_page = -2; }

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
