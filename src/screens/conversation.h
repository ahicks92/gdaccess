#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// An NPC conversation, built from the game's own step tree (world::conversation_state(), captured from the
// hooks on Conversation::GetText): type 0 = the root (speaker name), 1 = the NPC's speech, 2 = a player
// response. Drawn text is only the open/closed checkpoint and where a response sits for the click.
// Items: the speech (read on arrival), one per response (Enter picks it); Escape closes the dialog.
std::unique_ptr<gd::core::Screen> make_conversation();
}  // namespace gd::screens
