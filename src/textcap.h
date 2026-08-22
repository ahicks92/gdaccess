#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Captures every 2D text draw the engine performs, per frame.
namespace gd::textcap {
struct Item {
  int x, y;             // anchor in window client coordinates (meaning depends on alignment)
  int xalign, yalign;   // GAME::GraphicsXAlign / GraphicsYAlign as passed (raw enum values)
  uint32_t rgba;        // primary color, 8 bits per channel (layout assumption: 4 floats r,g,b,a)
  std::u16string text;  // raw text, may contain ^x color codes
  const char* variant;  // which RenderText2d overload produced it
};
void on_text(Item&& item);     // called from the render hooks
void on_frame_end();           // called from Display::Update: rotate buffers, diff, log
std::vector<Item> snapshot();  // last completed frame, sorted top-to-bottom, left-to-right
// Checkpoint queries for screens' is_active(): is this exact (color-code-stripped) text on screen now?
// These read the game's drawn text only as a yes/no oracle; they are never a UI model.
bool has_text(std::string_view speakable_text);
bool find_text(std::string_view speakable_text, int& x, int& y);
// The drawn item for a label (position and colour); `last` = the lowest match when a label is drawn twice.
bool find_item(std::string_view speakable_text, Item& out, bool last = false);
void set_announce_changes(bool on);
bool announce_changes();
uint64_t frame();
std::string speakable(std::u16string_view raw);  // UTF-8 with the game's ^x color codes stripped
}  // namespace gd::textcap
