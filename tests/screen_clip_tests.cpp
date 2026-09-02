#include <doctest/doctest.h>
#include "core/screen_clip.h"

using namespace gd::core;

TEST_CASE("clip_toward: a target inside the window is returned as is") {
  float x = 0, y = 0;
  CHECK(clip_toward(800, 450, 600, 300, 1600, 900, 4, x, y));
  CHECK(x == doctest::Approx(600));
  CHECK(y == doctest::Approx(300));
}

TEST_CASE("clip_toward: a target past the right edge lands on the inset right edge along the line") {
  float x = 0, y = 0;
  CHECK(clip_toward(800, 450, 2400, 450, 1600, 900, 4, x, y));
  CHECK(x == doctest::Approx(1596));
  CHECK(y == doctest::Approx(450));
}

TEST_CASE("clip_toward: a diagonal target past two edges stops at the first edge crossed") {
  float x = 0, y = 0;
  // From the centre toward (-800, -150): the top edge (y = 4) is reached at t = 446/600 = 0.743; the left edge at
  // t = 796/1600 = 0.4975, so the left edge wins.
  CHECK(clip_toward(800, 450, -800, -150, 1600, 900, 4, x, y));
  CHECK(x == doctest::Approx(4));
  CHECK(y == doctest::Approx(450 - 600 * 0.4975));
  CHECK(y >= 4);
}

TEST_CASE("clip_toward: the mod's real failure, a target at (-86, 193) from the centre") {
  float x = 0, y = 0;
  CHECK(clip_toward(800, 450, -86, 193, 1600, 900, 4, x, y));
  CHECK(x == doctest::Approx(4));
  CHECK(y > 4);
  CHECK(y < 450);
}

TEST_CASE("clip_toward: a start outside the window is refused") {
  float x = 0, y = 0;
  CHECK_FALSE(clip_toward(-10, 450, 800, 450, 1600, 900, 4, x, y));
  CHECK_FALSE(clip_toward(800, 899, 800, 450, 1600, 900, 4, x, y));   // inside the window but inside the margin band
  CHECK_FALSE(clip_toward(2, 2, 3, 3, 4, 4, 4, x, y));                // a degenerate inset rectangle
}

TEST_CASE("clip_toward: a target on the start point stays put") {
  float x = 0, y = 0;
  CHECK(clip_toward(800, 450, 800, 450, 1600, 900, 4, x, y));
  CHECK(x == doctest::Approx(800));
  CHECK(y == doctest::Approx(450));
}
