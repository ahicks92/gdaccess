#pragma once
// Player settings that survive a session: a key=value text file at %LOCALAPPDATA%\gdaccess\settings.txt
// (the announcement toggles). Read once at load, rewritten on every change. Not the game's options.txt.
#include <string>
#include <string_view>

namespace gd::settings {
void init();
bool get_bool(std::string_view key, bool def);
void set_bool(std::string_view key, bool value);   // persists immediately
int get_int(std::string_view key, int def);
void set_int(std::string_view key, int value);
std::string path();
}
