#include "version_gate.h"
#include <windows.h>
#include <format>
#include "game_versions.h"

namespace gd::version {
namespace {
uint32_t pe_timestamp(HMODULE m) {
  if (!m) return 0;
  auto* dos = (IMAGE_DOS_HEADER*)m;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  auto* nt = (IMAGE_NT_HEADERS*)((char*)m + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  return nt->FileHeader.TimeDateStamp;
}
}  // namespace

Check check() {
  // Engine.dll and Game.dll are static imports of the exe, so both are mapped before this DLL's init runs.
  uint32_t exe = pe_timestamp(GetModuleHandleW(nullptr));
  uint32_t eng = pe_timestamp(GetModuleHandleW(L"Engine.dll"));
  uint32_t game = pe_timestamp(GetModuleHandleW(L"Game.dll"));
  Check c;
  c.exe_ts = std::format("{:x}", exe);
  c.engine_ts = std::format("{:x}", eng);
  c.game_ts = std::format("{:x}", game);
  for (const GameBuild& b : kSupportedBuilds)
    if (b.exe_ts == exe && b.engine_ts == eng && b.game_ts == game) { c.supported = true; c.name = b.name; break; }
  return c;
}

std::string describe(const Check& c) {
  return std::format("version: exe={} engine={} game={} -> {}", c.exe_ts, c.engine_ts, c.game_ts, c.supported ? c.name : "UNSUPPORTED");
}
}  // namespace gd::version
