#pragma once
// The game builds the mod is built against (src/version_gate.cpp). One row per build the exe layer's RVAs
// (exe_ui.cpp) and the Engine/Game object offsets were measured on; the PE header timestamps identify a build
// exactly (the exe's version resource does not change between patches). The archive in ../grim-dawn-archive
// carries the same numbers in its manifest.json. Add a row only once the mod WORKS on that build.
#include <cstdint>

namespace gd::version {
struct GameBuild {
  const char* name;      // spoken to the player
  uint32_t exe_ts;       // Grim Dawn.exe PE FileHeader.TimeDateStamp
  uint32_t engine_ts;    // Engine.dll
  uint32_t game_ts;      // Game.dll
};
inline constexpr GameBuild kSupportedBuilds[] = {
  {"1.3.0.8 Steam", 0x6a85fbec, 0x6a85fb5b, 0x6a85fbb3},   // archived 2026-08-26 as 1.3.0.8-6a85fbec
};
}  // namespace gd::version
