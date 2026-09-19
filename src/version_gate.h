#pragma once
// The version gate (2026-09-18): before any game hook is installed, the running exe + Engine.dll + Game.dll are
// matched by PE timestamp against src/game_versions.h. An unknown build gets one spoken refusal and NO hooks
// (the exe layer dies on any relink of the exe, the object offsets fail silently -- a stranger's crash with no
// diagnosis). GRIMDARK_ANY_VERSION=1 skips the refusal (dev, while measuring a new patch).
#include <string>

namespace gd::version {
struct Check {
  bool supported = false;
  const char* name = nullptr;   // the matched build, or null
  std::string exe_ts, engine_ts, game_ts;   // hex, for the log and the refusal line
};
Check check();
std::string describe(const Check& c);   // one log line
}  // namespace gd::version
