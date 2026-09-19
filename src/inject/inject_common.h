#pragma once
// Shared by gdinject (dev) and gdlaunch (player): find a process by image name, load a DLL into it.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

inline DWORD find_pid(const wchar_t* exe) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  DWORD pid = 0;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (!_wcsicmp(pe.szExeFile, exe)) { pid = pe.th32ProcessID; break; }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return pid;
}

// Run a kernel32 function in the target on a remote thread; returns its non-zero result or 0 on failure.
inline DWORD remote_call(HANDLE proc, const char* fn, LPVOID arg) {
  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  auto addr = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, fn);
  HANDLE t = CreateRemoteThread(proc, nullptr, 0, addr, arg, 0, nullptr);
  if (!t) { printf("CreateRemoteThread failed: %lu\n", GetLastError()); return 0; }
  WaitForSingleObject(t, 15000);
  DWORD code = 0;
  GetExitCodeThread(t, &code);
  CloseHandle(t);
  printf("%s returned 0x%lx\n", fn, code);
  return code;
}

// LoadLibraryW(full) inside the target. true on success.
inline bool inject_dll(HANDLE proc, const wchar_t* full) {
  SIZE_T bytes = (wcslen(full) + 1) * sizeof(wchar_t);
  LPVOID mem = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT, PAGE_READWRITE);
  if (!mem) { printf("VirtualAllocEx failed: %lu\n", GetLastError()); return false; }
  WriteProcessMemory(proc, mem, full, bytes, nullptr);
  DWORD r = remote_call(proc, "LoadLibraryW", mem);
  VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
  return r != 0;
}

// The install root is the parent of the x64\ folder the 64-bit exe sits in (database.arz and the .arc
// archives live there; the game crashes in a string compare without it as the working directory).
inline std::wstring install_root_of(const std::wstring& exe) {
  std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
  size_t slash = dir.find_last_of(L"\\/");
  std::wstring leaf = slash == std::wstring::npos ? dir : dir.substr(slash + 1);
  if (!_wcsicmp(leaf.c_str(), L"x64")) dir = dir.substr(0, slash);
  return dir;
}
