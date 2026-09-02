// The announcement toggles overlay (T in the world): a layered screen like the pet overlay. Two Tab stops:
// the switches (outgoing / incoming / telegraph cues -- the last a four-state: off, your target, highest tier,
// all; Enter cycles, Left/Right step) and the telegraph shapes (one on/off row per shape). State lives in
// combat / telegraph and is persisted through settings.
#include "screens/announcements.h"
#include <string>
#include <vector>
#include "combat.h"
#include "core/graph_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "screens/window_base.h"
#include "telegraph.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;
namespace {
bool g_open = false;

struct Toggle { const char* id; std::string_view label; bool (*get)(); void (*set)(bool); };   // the setters persist
const Toggle kToggles[] = {
  {"incoming", strings::kAnnounceIncoming, [] { return combat::incoming_enabled(); }, [](bool v) { combat::set_incoming(v); }},
  {"incomingHits", strings::kAnnounceIncomingHits, [] { return combat::incoming_hits_enabled(); }, [](bool v) { combat::set_incoming_hits(v); }},
};
void step_outgoing(int sign) {
  int m = (combat::outgoing_mode() + sign) % 3;
  if (m < 0) m += 3;
  combat::set_outgoing_mode(m);   // the navigator speaks the row's new value itself
}

void step_mode(int sign) {
  int m = ((int)telegraph::mode() + sign) % 4;
  if (m < 0) m += 4;
  telegraph::set_mode((telegraph::Mode)m);   // the navigator speaks the row's new value itself
}

class AnnouncementsScreen : public Screen {
 public:
  std::string_view key() const override { return "announcements"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kAnnouncements); }
  int layer() const override { return 1; }   // like the pet overlay: a game window covers and closes it
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }
  void on_unfocus() override { g_open = false; }

  void build(GraphBuilder& b) override {
    b.begin_stop("switches");
    b.push_context(strings::kAnnounceSwitches, strings::kList);   // a stop title: announced on entry, not a row
    auto out_row = row_item(std::string(strings::kAnnounceOutgoing), [] { return std::string(strings::kOutgoingModes[combat::outgoing_mode()]); },
                            [] { step_outgoing(+1); });   // Enter: next state (off / brief / full)
    out_row->on_adjust = [](int sign, bool) { step_outgoing(sign); };
    b.add_item(ControlId::structural("announce.outgoing"), out_row);
    for (const Toggle& t : kToggles) {
      const Toggle* tp = &t;
      b.add_item(ControlId::structural(std::string("announce.") + t.id),
                 row_item(std::string(t.label), [tp] { return std::string(tp->get() ? strings::kOn : strings::kOff); },
                          [tp] {   // Enter: flip + persist (the navigator speaks the new value)
                            tp->set(!tp->get());
                          }));
    }
    auto mode_row = row_item(std::string(strings::kAnnounceTelegraph), [] { return std::string(telegraph::mode_name(telegraph::mode())); },
                             [] { step_mode(+1); });   // Enter: next state
    mode_row->on_adjust = [](int sign, bool) { step_mode(sign); };   // Left/Right
    b.add_item(ControlId::structural("announce.telegraph"), mode_row);
    b.pop_context();

    b.begin_stop("shapes");
    b.push_context(strings::kAnnounceShapes, strings::kList);
    for (int i = 0; i < telegraph::kShapes; ++i) {
      b.add_item(ControlId::structural(std::string("telegraph.shape.") + telegraph::kShapeNames[i]),
                 row_item(std::string(strings::kTelegraphShapeLabels[i]), [i] { return std::string(telegraph::shape_enabled(i) ? strings::kOn : strings::kOff); },
                          [i] {
                            bool now = !telegraph::shape_enabled(i);
                            telegraph::set_shape_enabled(i, now);
                          }));
    }
    b.pop_context();
  }
};
}  // namespace

void open_announcements() { g_open = true; }
std::unique_ptr<Screen> make_announcements_overlay() { return std::make_unique<AnnouncementsScreen>(); }
}  // namespace gd::screens
