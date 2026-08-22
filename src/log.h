#pragma once
#include <format>
#include <string>
#include <string_view>
#include "ring.h"

namespace gd::log {
void init();  // %LOCALAPPDATA%\gdaccess\gdaccess.log (truncated per load)
void write(std::string_view line);
template <typename... A>
void writef(std::format_string<A...> fmt, A&&... a) { write(std::format(fmt, std::forward<A>(a)...)); }
std::string utf8(std::u16string_view s);
std::string utf8(const char16_t* s);
std::wstring path();
LineRing& ring();  // recent lines, for the dev server
}  // namespace gd::log
