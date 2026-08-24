#include <doctest/doctest.h>
#include <algorithm>
#include "core/sonar_field.h"

using namespace gd::core;

static bool fired(const std::vector<SonarField::Ping>& ps, unsigned id) {
  return std::any_of(ps.begin(), ps.end(), [id](const SonarField::Ping& p) { return p.id == id; });
}

TEST_CASE("field period: log map, near=fast far=slow, monotone") {
  FieldParams p;   // 0.14s @2u .. 0.80s @25u
  CHECK(p.period_for(1.0f) == doctest::Approx(0.14));    // clamped at/under dist_near
  CHECK(p.period_for(2.0f) == doctest::Approx(0.14));
  CHECK(p.period_for(25.0f) == doctest::Approx(0.80));   // clamped at/over dist_far
  CHECK(p.period_for(40.0f) == doctest::Approx(0.80));
  // The midpoint in log-distance is the geometric mean sqrt(2*25) ~= 7.07u -> half way in period.
  CHECK(p.period_for(7.0711f) == doctest::Approx(0.47).epsilon(0.02));
  // Monotone increasing with distance, and steeper up close: 2->4 costs more period than 12->14.
  CHECK(p.period_for(4.0f) > p.period_for(2.0f));
  CHECK(p.period_for(4.0f) - p.period_for(2.0f) > p.period_for(14.0f) - p.period_for(12.0f));
}

TEST_CASE("field: a new id is seeded (no fire on the first frame), then pulses on its period") {
  SonarField f;
  double T = f.params().period_for(2.0f);   // 0.14 (phase 0)
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.0).empty());   // first sighting: seed only
  CHECK(f.tracked() == 1);
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001).size() == 1);   // due at ~0 -> fires next frame
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001 + T * 0.5).empty());   // mid-period, quiet
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001 + T + 0.001).size() == 1);   // one period later
}

TEST_CASE("field: the phase offset staggers co-distant things") {
  SonarField f;
  double T = f.params().period_for(2.0f);
  // Two things at the same distance, one hard left (phase 0), one centre (phase 0.5), seen together.
  f.update({{1, 2.0f, 0.0f, 0}, {2, 2.0f, 0.5f, 0}}, 0.0);
  // Left is due at ~0, centre at ~0.5T. Just after seeding, only the left one fires.
  auto a = f.update({{1, 2.0f, 0.0f, 0}, {2, 2.0f, 0.5f, 0}}, 0.001);
  CHECK(fired(a, 1));
  CHECK_FALSE(fired(a, 2));
  // Half a period later the centre one fires and the left one does not (they never coincide).
  auto b = f.update({{1, 2.0f, 0.0f, 0}, {2, 2.0f, 0.5f, 0}}, 0.001 + T * 0.5);
  CHECK_FALSE(fired(b, 1));
  CHECK(fired(b, 2));
}

TEST_CASE("field: a long stall advances by whole periods, never bursts") {
  SonarField f;
  double T = f.params().period_for(2.0f);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.0);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.001);        // consume the seed
  auto a = f.update({{1, 2.0f, 0.0f, 0}}, 100.0);   // huge gap (a load): a single ping, not a machine-gun
  CHECK(a.size() == 1);
  // And it is back on its own grid: quiet immediately after, fires again ~T later.
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 100.0 + T * 0.5).empty());
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 100.0 + T + 0.001).size() == 1);
}

TEST_CASE("field: an absent id is forgotten, reset clears all") {
  SonarField f;
  f.update({{1, 2.0f, 0.0f, 0}, {2, 5.0f, 0.0f, 0}}, 0.0);
  CHECK(f.tracked() == 2);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.05);   // 2 dropped out
  CHECK(f.tracked() == 1);
  f.reset();
  CHECK(f.tracked() == 0);
}
