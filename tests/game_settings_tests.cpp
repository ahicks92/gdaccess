#include <doctest/doctest.h>
#include "core/game_settings.h"

using namespace gd::core::game_settings;

static const char* kOptions =
    "quickBuy                  = true\r\n"
    "movementType              = 0\r\n"
    "evadeFollowCursor         = true\r\n"
    "targetLock                = false\r\n";

TEST_CASE("patch_options: forces the two keys and keeps every other byte") {
  Patch p = patch_options(kOptions, required_options());
  CHECK(p.changed());
  CHECK(p.changes.size() == 2);
  CHECK(p.text ==
        "quickBuy                  = true\r\n"
        "movementType              = 1\r\n"
        "evadeFollowCursor         = false\r\n"
        "targetLock                = false\r\n");
  CHECK(p.changes[0] == "movementType 0 -> 1");
  CHECK(p.changes[1] == "evadeFollowCursor true -> false");
}

TEST_CASE("patch_options: a file already right is returned unchanged") {
  std::string ok = patch_options(kOptions, required_options()).text;
  Patch p = patch_options(ok, required_options());
  CHECK(!p.changed());
  CHECK(p.text == ok);
}

TEST_CASE("patch_options: absent keys are appended in the game's layout; a missing final newline is repaired") {
  Patch p = patch_options("quickBuy                  = true", required_options());
  CHECK(p.changes.size() == 2);
  CHECK(p.text ==
        "quickBuy                  = true\r\n"
        "movementType              = 1\r\n"
        "evadeFollowCursor         = false\r\n");
}

TEST_CASE("patch_options: an empty file becomes just the forced keys") {
  Patch p = patch_options("", required_options());
  CHECK(p.text == "movementType              = 1\r\nevadeFollowCursor         = false\r\n");
}

TEST_CASE("patch_options: LF files stay LF and odd spacing around = is preserved") {
  Patch p = patch_options("movementType=0\nother = 3\n", required_options());
  CHECK(p.text == "movementType= 1\nother = 3\nevadeFollowCursor         = false\n");
}

TEST_CASE("patch_keymap: an empty file gets the default map with WASD on 63..66") {
  Patch p = patch_keymap("");
  CHECK(p.changed());
  CHECK(p.text.find("63: 17 0\r\n64: 31 0\r\n65: 30 0\r\n66: 32 0\r\n67: 0 0\r\n") != std::string::npos);
  CHECK(p.text.find("1: 46 23\r\n") != std::string::npos);
  CHECK(p.text.rfind("0: 0 0\r\n", 0) == 0);
}

TEST_CASE("patch_keymap: unbound move lines are named and the whole file becomes the default map") {
  std::string in = "0: 0 0\r\n50: 33 0\r\n62: 0 0\r\n63: 0 0\r\n64: 0 0\r\n65: 44 0\r\n66: 0 12\r\n67: 0 0\r\n";
  Patch p = patch_keymap(in);
  CHECK(p.changes.size() == 2);
  CHECK(p.changes[0] == "key map: forward W was unbound");
  CHECK(p.changes[1] == "key map: backward S was unbound");
  CHECK(p.text == default_keymap_text());
}

TEST_CASE("patch_keymap: the game's own default file is returned unchanged") {
  std::string d = default_keymap_text();
  Patch p = patch_keymap(d);
  CHECK(!p.changed());
  CHECK(p.text == d);
}

TEST_CASE("patch_keymap: a rebound map with the move keys bound is reset with a generic reason") {
  std::string d = default_keymap_text();
  std::string in = d;
  in.replace(in.find("50: 0 0"), 7, "50: 33 0");
  Patch p = patch_keymap(in);
  CHECK(p.changes.size() == 1);
  CHECK(p.changes[0].find("differed") != std::string::npos);
  CHECK(p.text == d);
}

TEST_CASE("patch_keymap: LF line endings alone count as a difference (the game writes CRLF)") {
  std::string lf = default_keymap_text();
  size_t pos;
  while ((pos = lf.find("\r\n")) != std::string::npos) lf.replace(pos, 2, "\n");
  CHECK(patch_keymap(lf).changed());
}
