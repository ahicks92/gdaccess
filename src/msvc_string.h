#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Layout-compatible view of MSVC's std::basic_string<CharT> (VS2015+ ABI, x64):
// 16-byte SSO buffer / heap pointer union, then size, then capacity.
template <typename CharT>
struct MsvcString {
  union {
    CharT buf[16 / sizeof(CharT)];
    CharT* ptr;
  } u;
  size_t size;
  size_t capacity;

  const CharT* data() const { return capacity < (16 / sizeof(CharT)) ? u.buf : u.ptr; }
  std::basic_string_view<CharT> view() const { return {data(), size}; }
};
static_assert(sizeof(MsvcString<char>) == 32);
static_assert(sizeof(MsvcString<char16_t>) == 32);
using MsvcStringA = MsvcString<char>;
using MsvcStringW = MsvcString<char16_t>;  // the game uses basic_string<unsigned short>
