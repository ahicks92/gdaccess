#include <doctest/doctest.h>
#include "core/screen_clip.h"
#include <vector>

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

namespace { struct R { float x, y, w, h; }; }

TEST_CASE("back_off_rects: a target clear of every rect is returned as is") {
  std::vector<R> hud{{398, 781, 805, 119}};
  float x = 0, y = 0;
  CHECK(back_off_rects(800, 450, 800, 700, hud, 40, 0.15f, x, y));
  CHECK(x == doctest::Approx(800));
  CHECK(y == doctest::Approx(700));
}

TEST_CASE("back_off_rects: a target on the HUD backs off along the line to just above it") {
  std::vector<R> hud{{398, 781, 805, 119}};
  float x = 0, y = 0;
  CHECK(back_off_rects(800, 450, 800, 860, hud, 40, 0.15f, x, y));
  CHECK(x == doctest::Approx(800));
  CHECK(y < 781);
  CHECK(y > 760);   // the first sample above the HUD, not the player's own point
}

TEST_CASE("back_off_rects: a diagonal line keeps its bearing") {
  std::vector<R> hud{{398, 781, 805, 119}};
  float x = 0, y = 0;
  CHECK(back_off_rects(800, 450, 1000, 890, hud, 40, 0.15f, x, y));
  CHECK(y < 781);
  CHECK(x == doctest::Approx(800 + (y - 450) * 200 / 440).epsilon(0.01));
}

TEST_CASE("back_off_rects: refuses when only the player's own point is clear") {
  std::vector<R> hud{{0, 100, 1600, 800}};   // everything below y 100 is covered
  float x = 0, y = 0;
  CHECK_FALSE(back_off_rects(800, 450, 800, 700, hud, 40, 0.15f, x, y));
}
