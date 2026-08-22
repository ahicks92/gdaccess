#pragma once
#include <string>
#include <string_view>

// What the game drew over an enemy (a floating combat-text event) -> what to say. The game formats the text
// through its localization tags: HitFormat "{%.0f0}", CriticalHitFormat "{%.0f0}", CriticalHitFormatMult
// "{%.0f0} (x{%.2f1})" (the critMultipliers option), and the words tagMiss / tagDodge / tagBlock. The event
// also carries a text class (0x85 = the crit style). Engine-free, unit-tested.
namespace gd::core {
struct CombatText {
  bool is_number = false;
  double amount = 0;       // valid when is_number
  bool crit = false;
  std::string number;      // the digits as drawn, "(xN)" stripped: "456"
  std::string word;        // when !is_number: the drawn text verbatim ("Miss", "Dodge", "Block")
};
CombatText parse_combat_text(std::string_view drawn, bool crit_class);
}  // namespace gd::core
