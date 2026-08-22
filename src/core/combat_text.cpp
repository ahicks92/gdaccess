#include "combat_text.h"
#include <cctype>
#include <cstdlib>

namespace gd::core {
namespace {
std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
  while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
  return s;
}
}  // namespace

CombatText parse_combat_text(std::string_view drawn, bool crit_class) {
  CombatText out;
  std::string_view s = trim(drawn);
  out.crit = crit_class;
  // "456 (x1.50)": the multiplier suffix means a crit whatever the text class says.
  if (!s.empty() && s.back() == ')') {
    size_t open = s.rfind("(x");
    if (open != std::string_view::npos) { out.crit = true; s = trim(s.substr(0, open)); }
  }
  bool digits = !s.empty();
  for (char c : s)
    if (!std::isdigit((unsigned char)c) && c != ',' && c != '.' && c != ' ') { digits = false; break; }
  if (digits) {
    std::string clean;
    for (char c : s) if (std::isdigit((unsigned char)c) || c == '.') clean += c;
    if (!clean.empty() && clean != ".") {
      out.is_number = true;
      out.amount = std::strtod(clean.c_str(), nullptr);
      // Speak the integer part: the game draws "{%.0f0}", so this is what it drew.
      size_t dot = clean.find('.');
      out.number = dot == std::string::npos ? clean : clean.substr(0, dot);
      if (out.number.empty()) out.number = "0";
      return out;
    }
  }
  out.word = std::string(s);
  return out;
}
}  // namespace gd::core
