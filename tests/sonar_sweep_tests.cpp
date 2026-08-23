#include <doctest/doctest.h>
#include "core/sonar_sweep.h"

using namespace gd::core;

TEST_CASE("sweep gap: spread over the count, clamped") {
  SweepParams p;
  CHECK(p.gap_for(1) == doctest::Approx(0.20));   // 0.75 clamped to gap_max
  CHECK(p.gap_for(5) == doctest::Approx(0.15));
  CHECK(p.gap_for(20) == doctest::Approx(0.10));  // clamped to gap_min
}

TEST_CASE("sweep: left to right, one per slot, then a rest before the next sweep") {
  SonarSweep s;
  CHECK(s.wants_entries(0.0));
  s.begin({{1, 3.0f, 0}, {2, -5.0f, 0}, {3, 0.0f, 0}}, 0.0);
  CHECK_FALSE(s.wants_entries(0.0));
  auto a = s.next(0.0);
  REQUIRE(a); CHECK(a->id == 2);                 // leftmost first
  CHECK_FALSE(s.next(0.05));                     // gap for 3 = 0.25 -> clamped 0.20
  auto b = s.next(0.21);
  REQUIRE(b); CHECK(b->id == 3);
  auto c = s.next(0.45);
  REQUIRE(c); CHECK(c->id == 1);
  CHECK_FALSE(s.next(0.5));                      // sweep over
  CHECK_FALSE(s.wants_entries(0.5));             // resting (0.4 s)
  CHECK(s.wants_entries(0.86));
}

TEST_CASE("sweep: an empty list rests, a single item repeats every gap_max + rest") {
  SonarSweep s;
  s.begin({}, 1.0);
  CHECK_FALSE(s.wants_entries(1.2));
  CHECK(s.wants_entries(1.41));
  s.begin({{7, 0.0f, 1}}, 2.0);
  auto a = s.next(2.0);
  REQUIRE(a); CHECK(a->id == 7); CHECK(a->kind == 1);
  CHECK_FALSE(s.wants_entries(2.3));
  CHECK(s.wants_entries(2.41));
  s.reset();
  CHECK(s.wants_entries(2.41));
}
