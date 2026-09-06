#pragma once
// What a quest task handed out when it completed, captured from the game's own reward pipeline
// (docs/ingame-ui-survey.md "Quest reward"): Quest2Task::Complete evaluates the task's reward events and runs
// their actions through ScriptableActionCollection::Execute; the reward actions (ScriptableAction_Give*) are
// read there, before they run, so a GiveItem's generated items are still the ones about to be given. The
// quest reward screen (src/screens/modals.cpp) lists the latest record for the quest the game's window names.
// Game-thread hooks; the accessors copy under a mutex (the dev server reads from its own thread).
#include <string>
#include <vector>

namespace gd::quest_rewards {
enum class Kind { Experience, Money, Item, SkillPoints, AttributePoints, DevotionPoints, Levels, Tribute, Reputation, Unknown };
struct Reward {
  Kind kind = Kind::Unknown;
  long long amount = 0;      // experience / bits / points / levels / tribute / reputation; an item's stack count
  std::string name;          // the item's name, the faction's name; empty otherwise
  std::string text() const;  // the spoken row: "750 iron bits", "2 Vital Essence", "300 reputation, Devil's Crossing"
};
struct Record {
  std::string quest, task;   // the game's localized names
  bool quest_complete = false;   // the whole quest is done (the window says "Quest Complete"), else a step ("Quest Progress")
  std::vector<Reward> rewards;   // in the game's order
  unsigned long long frame = 0;
};

bool install();
void remove();
std::vector<Record> recent();                        // newest last, at most a handful
bool latest_for(const std::string& quest, Record& out);   // the newest record for that quest name; false when none
std::string status();                                // /rewards
}  // namespace gd::quest_rewards
