// gdinject: load (or unload) gdaccess.dll into the running Grim Dawn process.
//   gdinject.exe <path-to-dll>            inject
//   gdinject.exe --eject <path-to-dll>    FreeLibrary the module in the target
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <string>
#include "inject_common.h"

static HMODULE find_module(HANDLE proc, const std::wstring& fullpath) {
  static HMODULE mods[4096];
  DWORD needed = 0;
  if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &needed, LIST_MODULES_64BIT)) return nullptr;
  for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
    wchar_t name[MAX_PATH];
    if (GetModuleFileNameExW(proc, mods[i], name, MAX_PATH) && !_wcsicmp(name, fullpath.c_str())) return mods[i];
  }
  return nullptr;
}

// --launch <exe> <dll>: start the game suspended, inject, then let it run, so our hooks are in place
// before the game initializes input (its keyboard-hook / SystemParametersInfo code runs at startup).
static int launch(const wchar_t* exe, const wchar_t* dll) {
  wchar_t full[MAX_PATH];
  GetFullPathNameW(dll, MAX_PATH, full, nullptr);
  std::wstring dir = install_root_of(exe);   // Steam launches it from the install root; so must we
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
  // The Steam stub relaunches the exe through steam.exe unless it believes Steam started it; SteamAppId in the
  // environment is the documented signal (it is what Steam itself sets), and it keeps our injected process alive.
  SetEnvironmentVariableW(L"SteamAppId", L"219990");
  SetEnvironmentVariableW(L"SteamGameId", L"219990");
  // Never take the foreground: STARTF_USESHOWWINDOW + SW_SHOWMINNOACTIVE means the window comes up without
  // activating, so a launch does not interrupt the developer's screen reader (recipe from soundz/launch.ps1).
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNOACTIVATE;  // visible, never activated (the DLL blocks the game's own focus grabs)
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &pi)) {
    printf("CreateProcess failed: %lu\n", GetLastError());
    return 1;
  }
  printf("launched pid %lu (suspended), injecting %ls\n", pi.dwProcessId, full);
  int r = inject_dll(pi.hProcess, full) ? 0 : 1;
  ResumeThread(pi.hThread);
  printf("resumed\n");
  CloseHandle(pi.hThread);
  // Fallback: if the stub relaunched anyway, follow the new process and inject there as early as we can.
  DWORD mine = pi.dwProcessId;
  CloseHandle(pi.hProcess);
  for (int i = 0; i < 100; ++i) {
    Sleep(200);
    DWORD pid = find_pid(L"Grim Dawn.exe");
    HANDLE h = pid ? OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, mine) : nullptr;
    bool mine_alive = false;
    if (h) { DWORD code = 0; mine_alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE; CloseHandle(h); }
    if (pid && pid != mine && !mine_alive) {
      printf("stub relaunched the game as pid %lu; injecting there\n", pid);
      HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
      if (!proc) { printf("OpenProcess failed: %lu\n", GetLastError()); return 1; }
      return inject_dll(proc, full) ? 0 : 1;
    }
    if (!mine_alive && !pid) { printf("game exited without relaunching\n"); return 1; }
    if (i == 99) printf("original process still alive after 20 s; assuming no relaunch\n");
  }
  return r;
}

int wmain(int argc, wchar_t** argv) {
  if (argc >= 4 && !_wcsicmp(argv[1], L"--launch")) return launch(argv[2], argv[3]);
  bool eject = argc >= 3 && !_wcsicmp(argv[1], L"--eject");
  const wchar_t* rel = eject ? argv[2] : (argc >= 2 ? argv[1] : nullptr);
  if (!rel) { printf("usage: gdinject [--eject] <dll> | gdinject --launch <game.exe> <dll>\n"); return 2; }
  wchar_t full[MAX_PATH];
  GetFullPathNameW(rel, MAX_PATH, full, nullptr);
  DWORD pid = find_pid(L"Grim Dawn.exe");
  if (!pid) { printf("Grim Dawn.exe is not running\n"); return 1; }
  HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
  if (!proc) { printf("OpenProcess(%lu) failed: %lu\n", pid, GetLastError()); return 1; }
  printf("target pid %lu, dll %ls\n", pid, full);
  if (eject) {
    HMODULE m = find_module(proc, full);
    if (!m) { printf("module not loaded in target\n"); return 1; }
    // Run the DLL's orderly shutdown first (same image, same export RVA in every process), then FreeLibrary.
    HMODULE local = LoadLibraryExW(full, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    FARPROC unload = local ? GetProcAddress(local, "gdaccess_unload") : nullptr;
    if (unload) {
      LPVOID remote = (LPVOID)((uintptr_t)m + ((uintptr_t)unload - (uintptr_t)local));
      HANDLE t = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)remote, nullptr, 0, nullptr);
      if (t) { DWORD w = WaitForSingleObject(t, 15000); CloseHandle(t); printf("gdaccess_unload %s\n", w == WAIT_OBJECT_0 ? "done" : "TIMED OUT"); }
    } else {
      printf("warning: gdaccess_unload export not found; unloading without orderly shutdown\n");
    }
    if (local) FreeLibrary(local);
    return remote_call(proc, "FreeLibrary", (LPVOID)m) ? 0 : 1;
  }
  if (find_module(proc, full)) { printf("already loaded; eject first\n"); return 1; }
  return inject_dll(proc, full) ? 0 : 1;
}
