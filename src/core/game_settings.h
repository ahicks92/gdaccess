#pragma once
// The two game settings the mod cannot play without, applied to the game's own settings files as text before the
// game starts (gdlaunch does this; the game reads the files at launch and rewrites them with the same values on
// exit, so there is nothing to fight). Engine-free, unit-tested.
//
//   options.txt              `key<padding>= value` lines. movementType must be 1 (Keyboard: WASD is the game's own
//                            feature in that mode) and evadeFollowCursor false (docs/evade.md: on, Space dashes at
//                            the mouse cursor and ignores WASD). Every other line is kept byte for byte.
//   alternate_keybindings.txt  the keyboard-mode key map, `action: primary secondary` in the game's button codes.
//                            The game creates the four move actions (63..66 = forward, backward, left, right) UNBOUND
//                            when Movement Type is switched to Keyboard, and binds them only when the player presses
//                            the Keybinding tab's Default -- which also resets every other binding (measured
//                            2026-09-19). The mod's key scheme assumes the game's default map (docs/controls.md), so
//                            the launcher OWNS this file: it is written as the game's default keyboard map (with the
//                            move keys) whenever it differs. Players are told not to rebind (README).
#include <string>
#include <vector>

namespace gd::core::game_settings {

struct Patch {
  std::string text;                   // the file to write (equal to the input when nothing changed)
  std::vector<std::string> changes;   // one human line per change, for the launcher's console
  bool changed() const { return !changes.empty(); }
};

namespace detail {
inline std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
// Splits into lines keeping each line's own terminator; the last line may have none.
inline std::vector<std::string> split_lines(const std::string& t) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < t.size()) {
    size_t nl = t.find('\n', i);
    if (nl == std::string::npos) { out.push_back(t.substr(i)); break; }
    out.push_back(t.substr(i, nl + 1 - i));
    i = nl + 1;
  }
  return out;
}
inline std::string strip_eol(const std::string& line) {
  size_t n = line.size();
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) --n;
  return line.substr(0, n);
}
inline std::string eol_of(const std::string& t) { return t.find("\r\n") != std::string::npos || t.find('\n') == std::string::npos ? "\r\n" : "\n"; }
inline std::string join(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) out += l;
  return out;
}
}  // namespace detail

// options.txt: forces `key = want` for each (key, want) pair. A present key keeps its line's padding and terminator;
// an absent one is appended in the game's own layout (key padded to 26 columns). Empty input yields a file of just
// the forced keys.
inline Patch patch_options(const std::string& text, const std::vector<std::pair<std::string, std::string>>& wants) {
  using namespace detail;
  Patch p;
  auto lines = split_lines(text);
  const std::string eol = eol_of(text);
  std::vector<bool> seen(wants.size(), false);
  for (auto& line : lines) {
    std::string body = strip_eol(line);
    size_t eq = body.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(body.substr(0, eq));
    for (size_t w = 0; w < wants.size(); ++w) {
      if (key != wants[w].first) continue;
      seen[w] = true;
      std::string value = trim(body.substr(eq + 1));
      if (value == wants[w].second) break;
      std::string prefix = body.substr(0, eq + 1);
      std::string term = line.substr(body.size());
      line = prefix + " " + wants[w].second + term;
      p.changes.push_back(key + " " + (value.empty() ? std::string("(empty)") : value) + " -> " + wants[w].second);
      break;
    }
  }
  if (!lines.empty() && lines.back().find('\n') == std::string::npos) lines.back() += eol;   // a final line without a terminator
  for (size_t w = 0; w < wants.size(); ++w) {
    if (seen[w]) continue;
    std::string key = wants[w].first;
    if (key.size() < 26) key.resize(26, ' ');
    lines.push_back(key + "= " + wants[w].second + eol);
    p.changes.push_back(wants[w].first + " (absent) -> " + wants[w].second);
  }
  p.text = join(lines);
  return p;
}

// The settings the mod needs, in the game's own names and value spellings.
inline const std::vector<std::pair<std::string, std::string>>& required_options() {
  static const std::vector<std::pair<std::string, std::string>> v = {{"movementType", "1"}, {"evadeFollowCursor", "false"}};
  return v;
}

// The keyboard-mode move actions and the game's default keys for them (DIK scancodes: W S A D).
struct MoveBinding { int action; int key; const char* name; };
inline const MoveBinding* move_bindings(size_t& n) {
  static const MoveBinding b[] = {{63, 17, "forward W"}, {64, 31, "backward S"}, {65, 30, "left A"}, {66, 32, "right D"}};
  n = sizeof b / sizeof b[0];
  return b;
}

// The game's own default keyboard-mode map (what its Keybinding tab's Default writes in Keyboard movement mode,
// captured from a 1.3.0.8 install on 2026-09-19), CRLF like the game writes it.
inline std::string default_keymap_text() {
  static const int pairs[][2] = {
    {0, 0}, {46, 23}, {49, 0}, {16, 0}, {50, 154}, {28, 0}, {37, 0}, {34, 0}, {35, 0}, {2, 165}, {3, 167}, {4, 168}, {5, 164},
    {6, 155}, {7, 156}, {8, 0}, {9, 0}, {10, 0}, {11, 0}, {0, 0}, {0, 0}, {145, 0}, {146, 0}, {79, 0}, {81, 0}, {80, 0},
    {147, 0}, {51, 0}, {52, 0}, {18, 162}, {19, 161}, {82, 0}, {48, 0}, {38, 153}, {0, 159}, {56, 118}, {45, 0}, {44, 0},
    {29, 107}, {42, 163}, {25, 0}, {14, 0}, {43, 0}, {21, 160}, {60, 0}, {61, 0}, {62, 0}, {63, 0}, {64, 0}, {65, 0},
    {0, 0}, {0, 0}, {36, 0}, {47, 0}, {22, 0}, {0, 0}, {24, 0}, {0, 0}, {15, 0}, {57, 166}, {27, 0}, {0, 0}, {0, 0},
    {17, 0}, {31, 0}, {30, 0}, {32, 0}, {0, 0}};
  std::string out;
  for (int i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); ++i)
    out += std::to_string(i) + ": " + std::to_string(pairs[i][0]) + " " + std::to_string(pairs[i][1]) + "\r\n";
  return out;
}

// alternate_keybindings.txt: the launcher owns it -- anything but the game's default keyboard map (WASD included)
// is replaced by it. The change line says which move action was unbound when one was, else that the map differed.
inline Patch patch_keymap(const std::string& text) {
  using namespace detail;
  Patch p;
  p.text = default_keymap_text();
  if (text == p.text) return p;
  if (trim(text).empty()) { p.changes.push_back("no keyboard-mode key map; wrote the game's default one (WASD bound)"); return p; }
  size_t n = 0;
  const MoveBinding* moves = move_bindings(n);
  for (const auto& line : split_lines(text)) {
    std::string body = strip_eol(line);
    size_t colon = body.find(':');
    if (colon == std::string::npos) continue;
    int action = -1;
    try { action = std::stoi(trim(body.substr(0, colon))); } catch (...) { continue; }
    for (size_t m = 0; m < n; ++m)
      if (action == moves[m].action && trim(body.substr(colon + 1)) == "0 0") p.changes.push_back(std::string("key map: ") + moves[m].name + " was unbound");
  }
  if (p.changes.empty()) p.changes.push_back("key map differed from the game's default; reset (the mod's keys assume the defaults)");
  return p;
}

}  // namespace gd::core::game_settings
