#include "log.h"
#include <windows.h>
#include <cstdio>
#include <mutex>

namespace gd::log {
static FILE* g_file;
static std::mutex g_mu;
static std::wstring g_path;
static LineRing g_ring(2000);

std::wstring path() { return g_path; }
LineRing& ring() { return g_ring; }

void init() {
  wchar_t base[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
  std::wstring dir = n ? std::wstring(base, n) + L"\\gdaccess" : L"C:\\gdaccess";
  CreateDirectoryW(dir.c_str(), nullptr);
  g_path = dir + L"\\gdaccess.log";
  g_file = _wfopen(g_path.c_str(), L"w");
}

void write(std::string_view line) {
  SYSTEMTIME t; GetLocalTime(&t);
  std::string stamped = std::format("{:02}:{:02}:{:02}.{:03} {}", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
  g_ring.push(stamped);
  std::lock_guard lk(g_mu);
  if (!g_file) return;
  fprintf(g_file, "%s\n", stamped.c_str());
  fflush(g_file);
}

std::string utf8(std::u16string_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
  return out;
}
std::string utf8(const char16_t* s) { return s ? utf8(std::u16string_view(s)) : std::string(); }
}  // namespace gd::log
