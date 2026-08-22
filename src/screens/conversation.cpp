#include "screens/conversation.h"
#include <format>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "hooks.h"
#include "screens/controls.h"
#include "speech.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

static constexpr std::string_view kScreen = "conversation";
static const ControlType kSpeechType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};
// ConversationStep::Type as observed 2026-08-21: 0 root/speaker, 1 NPC speech, 2 player response
// (tagContinue), 6 end-of-conversation response (tagEndConversation, e.g. "(Receive Item: ...)"). Anything
// that is not the root or the speech is treated as a response.
constexpr int kRoot = 0, kSpeech = 1;

struct Node { std::string speaker, speech; std::vector<std::pair<void*, std::string>> responses; bool open = false; };

// The current node from the capture, and whether the game still shows it (a response row, or the speaker
// line, drawn on screen -- the capture happens once per node, so the drawn text is the open/closed oracle).
static Node current() {
  Node n;
  world::ConvState s = world::conversation_state();
  for (const world::ConvStep& st : s.steps) {
    if (st.type == kRoot) n.speaker = st.text;
    else if (st.type == kSpeech) n.speech = st.text;
    else if (st.available) n.responses.push_back({st.step, st.text});
  }
  for (auto& [step, text] : n.responses) if (textcap::has_text(text)) { n.open = true; break; }
  if (!n.open && !n.speaker.empty() && !n.speech.empty() && textcap::has_text(n.speaker)) n.open = true;
  return n;
}

class ConversationScreen : public Screen {
 public:
  std::string_view key() const override { return "conversation"; }
  bool is_active() override { return current().open; }
  std::string screen_name() const override { return std::string(strings::kConversation); }
  int layer() const override { return 30; }
  bool exclusive() const override { return true; }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { hooks::push_key(0x01, false, false, false, 0); }}};  // the game's Escape closes the dialog
  }
  void on_focus() override {
    Screen::on_focus();
    Node n = current();
    MessageBuilder m;
    if (!n.speaker.empty()) m.fragment(n.speaker);
    if (!n.speech.empty()) m.list_item().fragment(n.speech);
    speech::speak(m.build(), false);
    last_speech_ = n.speech;
  }
  // A new node in the same dialog (a response picked): read the new speech, keep focus on the responses.
  void on_update() override {
    Node n = current();
    if (n.speech != last_speech_) {
      last_speech_ = n.speech;
      MessageBuilder m;
      if (!n.speaker.empty()) m.fragment(n.speaker);
      m.list_item().fragment(n.speech);
      speech::speak(m.build(), true);
    }
  }
  void build(GraphBuilder& b) override {
    Node n = current();
    b.begin_stop("dialog");
    {
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kSpeechType;
      std::string text = n.speaker.empty() ? n.speech : n.speaker + ": " + n.speech;
      v->announcements = {NodeAnnouncement([text] { return text; }, false, announcement_kinds::kValue)};
      b.add_item(ControlId::structural("conversation.speech"), v);
    }
    b.begin_stop("responses");
    for (size_t i = 0; i < n.responses.size(); ++i) {
      std::string text = n.responses[i].second;
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kButtonType;
      v->announcements = {NodeAnnouncement([text] { return text; }, false, announcement_kinds::kLabel)};
      v->on_activate = [text] { click_label(kScreen, text); };  // the response's drawn row
      b.add_item(ControlId::structural(std::format("conversation.response{}", i)), v);
    }
  }
  // Land on the responses, not the speech: the speech was just read.
  bool start_unfocused() const override { return false; }

 private:
  std::string last_speech_;
};

std::unique_ptr<Screen> make_conversation() { return std::make_unique<ConversationScreen>(); }
}  // namespace gd::screens
