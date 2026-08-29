#include "screens/modals.h"
#include <format>
#include "gameapi.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::WidgetB;

namespace {
// A window's text element / button at an offset (by-value members; docs/ingame-ui-survey.md).
WidgetB at(const exe_ui::WindowB& w, unsigned off) { return {w ? (char*)w.p + off : nullptr}; }
void add_text(GraphBuilder& b, const std::string& id, const WidgetB& t) {
  std::string s = textcap::speakable(t.text());
  if (!s.empty()) b.add_item(ControlId::structural(id), line_item(s));
}
void add_button(GraphBuilder& b, const std::string& id, const WidgetB& btn, void* registry, std::string fallback_label = {}) {
  std::string label = textcap::speakable(btn.text());
  if (label.empty()) label = fallback_label;
  bool enabled = btn.enabled();
  auto v = std::make_shared<NodeVtable>();
  v->control_type = &kButtonType;
  v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                      NodeAnnouncement([enabled] { return enabled ? std::string() : std::string(strings::kDisabled); }, true, announcement_kinds::kEnabled)};
  v->on_activate = [btn, registry, enabled] { if (!enabled) { speech::speak(strings::kDisabled, true); return; } btn.press(registry); };
  b.add_item(ControlId::structural(id), v);
}
}  // namespace

// Quest reward (InGameUI+0x8efd8): questTitleString +0x1b8, questNameString +0x2b0, XPValue +0x7e0, acceptButton
// +0x388 through registry +0x738. Rewards are the quest's own (Quest2Event text) -- nothing to choose, only Accept.
class QuestRewardScreen : public WindowScreen {
 public:
  QuestRewardScreen() : WindowScreen("quest_reward", std::string(strings::kQuestReward), exe_ui::ingame::kQuestReward, 26) {}
  bool exclusive() const override { return true; }
  void build(GraphBuilder& b) override {
    exe_ui::WindowB w = window();
    if (!w) return;
    b.begin_stop("page");
    add_text(b, "reward.title", at(w, 0x1b8));
    add_text(b, "reward.name", at(w, 0x2b0));
    add_text(b, "reward.xp", at(w, 0x7e0));
    add_button(b, "reward.accept", at(w, 0x388), (char*)w.p + 0x738, std::string(strings::kAccept));
  }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [this] { exe_ui::WindowB w = window(); if (w) at(w, 0x388).press((char*)w.p + 0x738); }}};
  }
};

// Shrine: title +0x540, info +0x638, offering boxes +0x8e0 / +0xbd0 / +0xec0 (their text elements), shrine
// button +0x11f8, cancel +0x15a8, close +0x1958, registry +0x11b0, the shrine object's id +0xa4. TWO windows of
// this shape exist (found live 2026-08-28): the ruined shrine (InGameUI+0x7da50, "Offer" + offerings) and the
// desecrated one (InGameUI+0x7f6f8, its own class with "shrineCorruptedBitmap", info = tagShrineConfirmProxy
// "Summon what is trapped within?", button "Start"). The game shows the inventory window alongside both; this
// screen's layer (24) outranks it. Button labels come from the widgets' own captions.
class ShrineScreen : public WindowScreen {
 public:
  ShrineScreen(const char* key, unsigned window_off) : WindowScreen(key, std::string(strings::kShrine), window_off, 24) {}
  void build(GraphBuilder& b) override {
    exe_ui::WindowB w = window();
    if (!w) return;
    b.begin_stop("page");
    add_text(b, "shrine.title", at(w, 0x540));
    add_text(b, "shrine.info", at(w, 0x638));
    // What it asks for: the game's own offering names off the shrine object the window shows (its id at +0xa4,
    // read by the exe's fill at exe+0x1d170a). The three offering boxes (+0x8e0/+0xbd0/+0xec0) are item icons;
    // their text elements read empty (the user's report 2026-08-26).
    unsigned shrine_id = 0; exe_ui::peek_u32((char*)w.p + 0xa4, shrine_id);
    int i = 0;
    for (const std::string& name : gameapi::shrine_offerings(shrine_id)) {
      MessageBuilder m; m.fragment(strings::kOffering).fragment(std::format("{}", ++i)).list_item().fragment(name);
      b.add_item(ControlId::structural(std::format("shrine.offer{}", i)), line_item(m.build()));
    }
    if (!i) for (unsigned off : {0x8e0u, 0xbd0u, 0xec0u}) add_text(b, std::format("shrine.offer{}", i++), at(w, off));
    void* reg = (char*)w.p + 0x11b0;
    add_button(b, "shrine.use", at(w, 0x11f8), reg, std::string(strings::kOffer));
    add_button(b, "shrine.cancel", at(w, 0x15a8), reg, std::string(strings::kClose));
  }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [this] { exe_ui::WindowB w = window(); if (w && !at(w, 0x1958).press((char*)w.p + 0x11b0)) w.show(false); }}};
  }
};

std::unique_ptr<Screen> make_quest_reward() { return std::make_unique<QuestRewardScreen>(); }
std::unique_ptr<Screen> make_shrine() { return std::make_unique<ShrineScreen>("shrine", exe_ui::ingame::kShrine); }
std::unique_ptr<Screen> make_corrupted_shrine() { return std::make_unique<ShrineScreen>("shrine_corrupted", exe_ui::ingame::kShrineCorrupted); }
}  // namespace gd::screens
