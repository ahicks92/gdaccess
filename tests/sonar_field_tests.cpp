#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
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
  SonarField f; f.params().hash_phase = false;   // the host phase (0 here) as the seed, for exact timing
  double T = f.params().period_for(2.0f);   // 0.14 (phase 0)
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.0).empty());   // first sighting: seed only
  CHECK(f.tracked() == 1);
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001).size() == 1);   // due at ~0 -> fires next frame
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001 + T * 0.5).empty());   // mid-period, quiet
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001 + T + 0.001).size() == 1);   // one period later
}

TEST_CASE("field: the phase offset staggers co-distant things") {
  SonarField f; f.params().hash_phase = false;
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

TEST_CASE("field: a blink at the radius edge keeps the phase grid (one pulse on return, then the old grid)") {
  SonarField f; f.params().hash_phase = false;
  double T = f.params().period_for(2.0f);   // 0.14
  f.update({{1, 2.0f, 0.0f, 0}}, 0.0);
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 0.001).size() == 1);   // grid: 0.001 + n*T
  for (double t = 0.05; t < 1.0; t += 0.05) CHECK(f.update({}, t).empty());   // gone for a second (within the grace)
  CHECK(f.tracked() == 1);
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 1.2).size() == 1);   // back and overdue: one pulse, no reseed, no burst
  // Still on the original grid: the next grid point after 1.2 is 0.001 + 9*T = 1.261.
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 1.25).empty());
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 1.27).size() == 1);
  // Without the grace it would have been reseeded a phase-offset in and stayed silent on return.
  SonarField g; g.params().grace_s = 0.0; g.params().hash_phase = false;
  g.update({{1, 2.0f, 0.0f, 0}}, 0.0); g.update({{1, 2.0f, 0.0f, 0}}, 0.001);
  g.update({}, 0.5);
  CHECK(g.tracked() == 0);
}

TEST_CASE("field: a long stall advances by whole periods, never bursts") {
  SonarField f; f.params().hash_phase = false;
  double T = f.params().period_for(2.0f);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.0);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.001);        // consume the seed
  auto a = f.update({{1, 2.0f, 0.0f, 0}}, 100.0);   // huge gap (a load): a single ping, not a machine-gun
  CHECK(a.size() == 1);
  // And it is back on its own grid: quiet immediately after, fires again ~T later.
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 100.0 + T * 0.5).empty());
  CHECK(f.update({{1, 2.0f, 0.0f, 0}}, 100.0 + T + 0.001).size() == 1);
}

TEST_CASE("field: an absent id is kept through the grace period, then forgotten; reset clears all") {
  SonarField f;   // grace 1.5 s
  f.update({{1, 2.0f, 0.0f, 0}, {2, 5.0f, 0.0f, 0}}, 0.0);
  CHECK(f.tracked() == 2);
  f.update({{1, 2.0f, 0.0f, 0}}, 0.05);   // 2 dropped out: still remembered
  CHECK(f.tracked() == 2);
  f.update({{1, 2.0f, 0.0f, 0}}, 1.6);    // unseen for longer than the grace: gone
  CHECK(f.tracked() == 1);
  f.reset();
  CHECK(f.tracked() == 0);
}

// ---- staggering: hash seed + collision push ----
#include <map>
using Trains = std::map<unsigned, std::vector<double>>;   // id -> the times it fired, from ONE run of the field
static Trains run(SonarField& f, const std::vector<SonarField::Item>& items, double t_end, double dt = 0.001) {
  Trains ts;
  for (double t = 0; t <= t_end; t += dt) for (const auto& p : f.update(items, t)) ts[p.id].push_back(t);
  return ts;
}
static double min_gap(const std::vector<double>& a, const std::vector<double>& b) {   // closest pair across the two trains
  double m = 1e9;
  for (double x : a) for (double y : b) m = std::min(m, std::fabs(x - y));
  return m;
}

TEST_CASE("stagger: two things on the same flank at the same distance no longer fire together (hash seed)") {
  SonarField f; f.params().collide_s = 0.0;   // seed alone
  std::vector<SonarField::Item> items{{100, 3.0f, 1.0f, 0}, {101, 3.0f, 1.0f, 0}};   // both pan +1.0 (the scarab pack)
  Trains t = run(f, items, 3.0);
  CHECK(t[100].size() > 5); CHECK(t[101].size() > 5);
  CHECK(min_gap(t[100], t[101]) > 0.03);   // the old pan seed gave 0 here
}

TEST_CASE("stagger: things drifting through each other are pushed apart by the collision window") {
  // Periods 0.21 s and 0.31 s (3.0 u and 6.0 u): free-running they coincide every ~0.65 s.
  std::vector<SonarField::Item> items{{1, 3.0f, 0.0f, 0}, {2, 6.0f, 0.0f, 0}};
  SonarField free; free.params().collide_s = 0.0; free.params().hash_phase = false;
  Trains fr = run(free, items, 20.0);
  const auto &fa = fr[1], &fb = fr[2];
  CHECK(min_gap(fa, fb) < 0.002);   // they do land on each other
  SonarField f; f.params().hash_phase = false;   // window 0.04 (default)
  Trains tr = run(f, items, 20.0);
  const auto &a = tr[1], &b = tr[2];
  CHECK(min_gap(a, b) >= 0.039);
  // The push costs little: pulse counts within a few percent of free-running.
  CHECK(a.size() >= fa.size() * 0.9); CHECK(b.size() >= fb.size() * 0.9);
  // And a thing's own cadence is still its period, give or take the window.
  double T1 = f.params().period_for(3.0f);
  for (size_t i = 1; i < a.size(); ++i) { CHECK(a[i] - a[i - 1] >= T1 - 0.002); CHECK(a[i] - a[i - 1] <= T1 + 0.045 * 3); }
}

TEST_CASE("stagger: different kinds never push each other") {
  std::vector<SonarField::Item> items{{1, 3.0f, 0.0f, 0}, {2, 3.0f, 0.0f, 1}};   // same distance, same seed, kinds 0 and 1
  SonarField f; f.params().hash_phase = false;
  Trains t = run(f, items, 3.0);
  CHECK(min_gap(t[1], t[2]) < 0.002);   // an enemy and a loot pulse may coincide: different sounds
}

TEST_CASE("stagger: a pack of eight co-distant things spreads over the period, each still on its own cadence") {
  std::vector<SonarField::Item> items;
  for (unsigned i = 0; i < 8; ++i) items.push_back({500 + i, 2.6f, 1.0f, 0});   // T ~ 0.21 s, window 0.04: it just fits
  SonarField f;
  Trains t = run(f, items, 4.0);
  for (unsigned i = 0; i < 8; ++i) for (unsigned j = i + 1; j < 8; ++j) CHECK(min_gap(t[500 + i], t[500 + j]) >= 0.039);
  for (unsigned i = 0; i < 8; ++i) CHECK(t[500 + i].size() >= 10);
}
