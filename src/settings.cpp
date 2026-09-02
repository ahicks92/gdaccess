#include "settings.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include "log.h"

namespace gd::settings {
namespace {
std::map<std::string, std::string> g_values;
std::mutex g_mu;
std::wstring g_path;

void save_locked() {
  FILE* f = _wfopen(g_path.c_str(), L"w");
  if (!f) { log::writef("settings: cannot write {}", path()); return; }
  for (auto& [k, v] : g_values) fprintf(f, "%s=%s\n", k.c_str(), v.c_str());
  fclose(f);
}
}  // namespace

void init() {
  wchar_t base[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
  std::wstring dir = n ? std::wstring(base, n) + L"\\gdaccess" : L"C:\\gdaccess";
  CreateDirectoryW(dir.c_str(), nullptr);
  g_path = dir + L"\\settings.txt";
  std::lock_guard<std::mutex> l(g_mu);
  g_values.clear();
  if (FILE* f = _wfopen(g_path.c_str(), L"r")) {
    char line[512];
    while (fgets(line, sizeof line, f)) {
      std::string s(line);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      size_t eq = s.find('=');
      if (eq == std::string::npos || eq == 0 || s[0] == '#') continue;
      g_values[s.substr(0, eq)] = s.substr(eq + 1);
    }
    fclose(f);
  }
  log::writef("settings: {} ({} values)", path(), g_values.size());
}

bool get_bool(std::string_view key, bool def) {
  std::lock_guard<std::mutex> l(g_mu);
  auto it = g_values.find(std::string(key));
  if (it == g_values.end()) return def;
  return it->second == "1" || it->second == "true" || it->second == "on";
}

void set_bool(std::string_view key, bool value) {
  std::lock_guard<std::mutex> l(g_mu);
  g_values[std::string(key)] = value ? "1" : "0";
  save_locked();
}

int get_int(std::string_view key, int def) {
  std::lock_guard<std::mutex> l(g_mu);
  auto it = g_values.find(std::string(key));
  if (it == g_values.end() || it->second.empty()) return def;
  return atoi(it->second.c_str());
}

void set_int(std::string_view key, int value) {
  std::lock_guard<std::mutex> l(g_mu);
  g_values[std::string(key)] = std::to_string(value);
  save_locked();
}

std::string path() {
  std::string out;
  for (wchar_t c : g_path) out += c < 128 ? (char)c : '?';
  return out;
}
}  // namespace gd::settings
