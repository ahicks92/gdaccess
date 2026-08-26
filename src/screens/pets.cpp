// Pets in the world (docs/pets.md): the Backspace overlay, the F2-F7 selection, the pet event announcer.
// Everything goes through gameapi_pets.cpp (exports only, no exe layer); the game's own selection list is not used.
#include "screens/pets.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "gameapi.h"
#include "screens/window_base.h"
#include "speech.h"
#include "voice.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace {
bool g_open = false;
std::set<unsigned> g_selected;                 // our own selection; one-shot like the game's (cleared by a command)
std::map<unsigned, std::string> g_known;       // pet id -> label when first seen (a dying pet may lose its label)
bool g_seeded = false;

std::vector<gameapi::PetInfo> live_pets() { return gameapi::pets(); }
void prune_selection(const std::vector<gameapi::PetInfo>& ps) {
  std::erase_if(g_selected, [&](unsigned id) { return std::none_of(ps.begin(), ps.end(), [id](const gameapi::PetInfo& p) { return p.id == id; }); });
}
void say_info(const std::string& line) { voice::say({voice::Which::Zira, line, 0.0f, 1.0f, voice::Policy::Overlap, voice::kGroupInfo}); }
// The pets a command applies to: the selection when there is one, else every pet (the game's Pet Attack rule).
std::vector<unsigned> command_targets(const std::vector<gameapi::PetInfo>& ps) {
  std::vector<unsigned> ids;
  for (const gameapi::PetInfo& p : ps) if (g_selected.empty() || g_selected.count(p.id)) ids.push_back(p.id);
  return ids;
}
std::string selection_word(const std::vector<gameapi::PetInfo>& ps) {
  if (g_selected.empty()) return std::string(strings::kAllPets);
  if (g_selected.size() == 1) for (const gameapi::PetInfo& p : ps) if (g_selected.count(p.id)) return p.label;
  MessageBuilder m; m.fragment(std::format("{}", g_selected.size())).fragment(strings::kSelectedPets); return m.build();
}
bool do_attack(std::vector<gameapi::PetInfo>& ps) {
  unsigned target = world::locked_target();
  if (!target || world::is_point_id(target)) { speech::speak(strings::kNoTarget, true); return false; }
  bool any = false;
  for (unsigned id : command_targets(ps)) any = gameapi::pet_attack(id, target) || any;
  MessageBuilder m; m.fragment(selection_word(ps)).fragment(any ? strings::kPetsAttack : strings::kCannot);
  speech::speak(m.build(), true);
  g_selected.clear();
  return any;
}
bool do_recall(std::vector<gameapi::PetInfo>& ps) {
  world::Vec3 me; if (!world::player_position(me)) return false;
  bool any = false;
  for (unsigned id : command_targets(ps)) any = gameapi::pet_move(id, me) || any;
  MessageBuilder m; m.fragment(selection_word(ps)).fragment(any ? strings::kPetsRecall : strings::kCannot);
  speech::speak(m.build(), true);
  g_selected.clear();
  return any;
}
}  // namespace

void open_pet_overlay() {
  if (gameapi::pet_ids().empty()) { speech::speak(strings::kNoPets, true); return; }
  g_open = true;
}
void toggle_pet_selected(int index) {
  std::vector<gameapi::PetInfo> ps = live_pets();
  prune_selection(ps);
  if (index < 0 || index >= (int)ps.size()) { speech::speak(strings::kNoPets, true); return; }
  unsigned id = ps[(size_t)index].id;
  bool now = !g_selected.count(id);
  if (now) g_selected.insert(id); else g_selected.erase(id);
  MessageBuilder m; m.fragment(ps[(size_t)index].label).fragment(now ? strings::kSelected : strings::kDeselected);
  speech::speak(m.build(), true);
}
void select_all_pets() {
  std::vector<gameapi::PetInfo> ps = live_pets();
  if (ps.empty()) { speech::speak(strings::kNoPets, true); return; }
  g_selected.clear();
  for (const gameapi::PetInfo& p : ps) g_selected.insert(p.id);
  MessageBuilder m; m.fragment(strings::kAllPets).fragment(strings::kSelected);
  speech::speak(m.build(), true);
}
void pets_attack_locked() {
  std::vector<gameapi::PetInfo> ps = live_pets();
  if (ps.empty()) { speech::speak(strings::kNoPets, true); return; }
  prune_selection(ps);
  do_attack(ps);
}
void pets_tick() {
  std::vector<unsigned> ids = gameapi::pet_ids();
  if (!g_seeded) {   // first look (entering the world / after a reload): learn silently
    g_seeded = true;
    for (unsigned id : ids) g_known[id] = world::label_of(id);
    return;
  }
  for (unsigned id : ids)
    if (!g_known.count(id)) {
      std::string label = world::label_of(id);
      g_known[id] = label;
      MessageBuilder m; strings::push_pet_event(m, label.empty() ? std::string(strings::kPets) : label, strings::kPetSummoned);
      say_info(m.build());
    }
  for (auto it = g_known.begin(); it != g_known.end();) {
    if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
      MessageBuilder m; strings::push_pet_event(m, it->second.empty() ? std::string(strings::kPets) : it->second, strings::kPetDown);
      say_info(m.build());
      g_selected.erase(it->first);
      it = g_known.erase(it);
    } else ++it;
  }
}
void pets_reset() { g_known.clear(); g_selected.clear(); g_seeded = false; g_open = false; }

class PetOverlayScreen : public Screen {
 public:
  std::string_view key() const override { return "pets"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kPets); }
  int layer() const override { return 1; }   // like the hotbar manager: a game window covers and closes it
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }
  void on_unfocus() override { g_open = false; }

  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    std::vector<gameapi::PetInfo> ps = live_pets();
    prune_selection(ps);
    if (ps.empty()) { g_open = false; return; }
    for (const gameapi::PetInfo& p : ps) {
      unsigned id = p.id;
      std::string label = p.label;
      auto v = row_item(label,
                        [id] {   // value: stance, "selected"
                          MessageBuilder m;
                          for (const gameapi::PetInfo& c : live_pets()) if (c.id == id) m.list_item().fragment(gameapi::pet_stance_name(c.stance));
                          if (g_selected.count(id)) m.list_item().fragment(strings::kSelected);
                          return m.build();
                        },
                        [id, label] {   // Enter: toggle selected
                          bool now = !g_selected.count(id);
                          if (now) g_selected.insert(id); else g_selected.erase(id);
                          MessageBuilder m; m.fragment(label).fragment(now ? strings::kSelected : strings::kDeselected);
                          speech::speak(m.build(), true);
                        },
                        [id] {   // Space: where it is
                          world::Vec3 me; world::player_position(me);
                          for (const gameapi::PetInfo& c : live_pets()) if (c.id == id) {
                            float d = std::sqrt((c.pos.x - me.x) * (c.pos.x - me.x) + (c.pos.z - me.z) * (c.pos.z - me.z));
                            MessageBuilder m; strings::push_distance_bearing(m, d, world::clock_hour(c.pos));
                            speech::speak(m.build(), true);
                          }
                        },
                        [id, label] {   // Backspace: disband (forgotten first, so the tick does not also say "down")
                          bool ok = gameapi::release_pet(id);
                          if (ok) { g_known.erase(id); g_selected.erase(id); }
                          MessageBuilder m; m.fragment(label).fragment(ok ? strings::kDisbanded : strings::kCannot);
                          speech::speak(m.build(), true);
                        });
      v->on_adjust = [id](int sign, bool) {   // Left/Right: stance (per summoning skill; every pet of it follows)
        for (const gameapi::PetInfo& c : live_pets()) if (c.id == id) gameapi::set_pet_stance(id, ((c.stance + sign) % 3 + 3) % 3);
      };
      b.add_item(ControlId::structural(std::format("pet.{}", id)), v);
    }
    b.add_item(ControlId::structural("pet.attack"), row_item(std::string(strings::kPetsAttack), [] { return selection_word(live_pets()); }, [] {
      std::vector<gameapi::PetInfo> cur = live_pets(); prune_selection(cur);
      if (do_attack(cur)) g_open = false;
    }));
    b.add_item(ControlId::structural("pet.recall"), row_item(std::string(strings::kPetsRecall), [] { return selection_word(live_pets()); }, [] {
      std::vector<gameapi::PetInfo> cur = live_pets(); prune_selection(cur);
      if (do_recall(cur)) g_open = false;
    }));
  }
};

std::unique_ptr<Screen> make_pet_overlay() { return std::make_unique<PetOverlayScreen>(); }
}  // namespace gd::screens
