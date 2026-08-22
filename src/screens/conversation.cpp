#include "screens/conversation.h"
#include <format>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "hooks.h"
#include "screens/controls.h"
#include "speech.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::ConvRow;
using exe_ui::ConvWindow;

static const ControlType kSpeechType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};

// The NPC conversation, read from the game's conversation window (docs/exe-ui-layout.md): the speaker, the
// full speech, and the response rows the game shows for this node (a single "Continue" while the speech is
// paginated, one per available response, or "End conversation"). Choosing a row is a click at the row's own
// rectangle -- the game's click path runs the step's quest actions, so it is not bypassed.
class ConversationScreen : public Screen {
 public:
  std::string_view key() const override { return "conversation"; }
  bool is_active() override { return exe_ui::available() && exe_ui::conv_window().open(); }
  std::string screen_name() const override { return std::string(strings::kConversation); }
  int layer() const override { return 30; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { hooks::push_key(0x01, false, false, false, 0); }}};  // the game's Escape closes the dialog
  }
  void on_focus() override {
    Screen::on_focus();
    ConvWindow w = exe_ui::conv_window();
    last_speech_ = w.speech();
    speech::speak(narration(w), false);
  }
  // A new node in the same dialog (a response picked): read the new speech, keep focus on the responses.
  void on_update() override {
    ConvWindow w = exe_ui::conv_window();
    std::string s = w.speech();
    if (s != last_speech_) { last_speech_ = s; speech::speak(narration(w), true); }
  }
  void build(GraphBuilder& b) override {
    ConvWindow w = exe_ui::conv_window();
    if (!w) return;
    // Responses first: focus lands there (the speech was just narrated); the speech item follows for re-reading.
    b.begin_stop("responses");
    std::vector<ConvRow> rows = w.rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      ConvRow r = rows[i];
      std::string text = textcap::speakable(r.text());
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([text] { return text; }, false, announcement_kinds::kLabel)};
      v->on_activate = [w, r] { w.choose(r); };
      b.add_item(ControlId::structural(std::format("conversation.response{}", i)), v);
    }
    b.begin_stop("dialog");
    {
      std::string text = narration(w);
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kSpeechType;
      v->announcements = {NodeAnnouncement([text] { return text; }, false, announcement_kinds::kValue)};
      b.add_item(ControlId::structural("conversation.speech"), v);
    }
  }

 private:
  static std::string narration(const ConvWindow& w) {
    MessageBuilder m;
    gd::strings::push_speech(m, textcap::speakable(w.speaker()), textcap::speakable(w.speech()));
    return m.build();
  }
  std::string last_speech_;
};

std::unique_ptr<Screen> make_conversation() { return std::make_unique<ConversationScreen>(); }
}  // namespace gd::screens
