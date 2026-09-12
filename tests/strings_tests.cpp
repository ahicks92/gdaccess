#include <doctest/doctest.h>
#include "core/strings.h"

TEST_CASE("strip_markup removes the game's colour codes and turns line breaks into spaces") {
  CHECK(gd::strings::strip_markup("{^b}Burial Hill Entrance") == "Burial Hill Entrance");
  CHECK(gd::strings::strip_markup("^ySecure the {^w}Riftgate") == "Secure the Riftgate");
  CHECK(gd::strings::strip_markup("line one{^n}line two") == "line one line two");
  CHECK(gd::strings::strip_markup("a^nb") == "a b");
  CHECK(gd::strings::strip_markup("100^^ pure") == "100^ pure");
  CHECK(gd::strings::strip_markup("plain") == "plain");
  CHECK(gd::strings::strip_markup("") == "");
}
