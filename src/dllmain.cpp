#include <windows.h>
#include "app.h"
#include "audio.h"
#include "audio_mute.h"
#include "world.h"
#include "devserver.h"
#include "hooks.h"
#include "log.h"
#include "speech.h"

static bool env_flag(const wchar_t* name) {
  wchar_t v[8];
  return GetEnvironmentVariableW(name, v, 8) > 0 && v[0] == L'1';
}
static int env_int(const wchar_t* name, int def) {
  wchar_t v[16];
  return GetEnvironmentVariableW(name, v, 16) > 0 ? _wtoi(v) : def;
}

static DWORD WINAPI init_thread(LPVOID) {
  gd::log::init();
  gd::log::write("gdaccess: loaded");
  // GDACCESS_MUTE=1 (set by the dev launcher): speech is recorded but not voiced, game audio session muted.
  bool mute = env_flag(L"GDACCESS_MUTE");
  gd::speech::set_muted(mute);
  bool sp = gd::speech::init();
  gd::hooks::install();
  gd::world::install();
  gd::audio::init();
  gd::app::init();
  gd::dev::start(env_int(L"GDACCESS_PORT", 8791));
  gd::speech::speak(sp ? "G D Access loaded" : "G D Access loaded, no speech backend", true);
  if (mute) { Sleep(3000); gd::audio::mute_process(true); }  // give the game time to open its audio session
  return 0;
}

// Orderly shutdown, called by the injector on a remote thread BEFORE FreeLibrary. It must not run from
// DllMain: stopping the dev server joins a thread, and a thread cannot exit while DllMain holds the loader
// lock (measured: FreeLibrary from DllMain-side teardown deadlocked the unload thread).
extern "C" __declspec(dllexport) DWORD WINAPI gdaccess_unload(LPVOID) {
  gd::log::write("gdaccess: unloading");
  gd::dev::stop();
  gd::app::shutdown();
  gd::audio::shutdown();
  gd::world::remove();
  gd::hooks::remove();
  gd::speech::shutdown();
  gd::log::write("gdaccess: hooks removed, ready for FreeLibrary");
  return 1;
}

BOOL WINAPI DllMain(HMODULE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(h);
    HANDLE t = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
  } else if (reason == DLL_PROCESS_DETACH) {
    gd::log::write("gdaccess: unloaded");
  }
  return TRUE;
}
