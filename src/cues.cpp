#include "cues.h"
#include <algorithm>
#include <atomic>
#include <string>
#include "settings.h"

namespace gd::cues {
namespace {
constexpr const char* kCueKey[kCues] = {"cue.walls", "cue.hazards", "cue.enemies", "cue.loot", "cue.entrances", "cue.breakables", "cue.shrines", "cue.interactables"};
constexpr const char* kChannelKey[kChannels] = {"volume.walls", "volume.hazards", "volume.enemies", "volume.other", "volume.voice.mark", "volume.voice.zira"};
bool g_on[kCues];
std::atomic<int> g_volume[kChannels];   // atomic: the voice worker reads while the game thread writes
}  // namespace

void init() {
  for (int i = 0; i < kCues; ++i) g_on[i] = settings::get_bool(kCueKey[i], true);
  for (int i = 0; i < kChannels; ++i) g_volume[i] = std::clamp(settings::get_int(kChannelKey[i], 100), 0, 100);
}
bool enabled(Cue c) { return c >= 0 && c < kCues ? g_on[c] : true; }
void set_enabled(Cue c, bool on) {
  if (c < 0 || c >= kCues) return;
  g_on[c] = on;
  settings::set_bool(kCueKey[c], on);
}
int volume(Channel ch) { return ch >= 0 && ch < kChannels ? g_volume[ch].load() : 100; }
void set_volume(Channel ch, int percent) {
  if (ch < 0 || ch >= kChannels) return;
  g_volume[ch] = std::clamp(percent, 0, 100);
  settings::set_int(kChannelKey[ch], g_volume[ch]);
}
float gain(Channel ch) { return volume(ch) / 100.0f; }
}  // namespace gd::cues
