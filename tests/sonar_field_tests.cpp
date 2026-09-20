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

// ---- crowd compression (per kind) ----
static double contrib(float dist, const FieldParams& p) {   // the host's c = gain^2 / period with the default ear curve
  double g = 10.0 / (10.0 + dist); if (g < 0.2) g = 0.2;
  return g * g / p.period_for(dist);
}
static double db(float gain) { return 20.0 * std::log10(gain); }

TEST_CASE("compression: effective count is 1 when one thing dominates, n when n are equal") {
  CHECK(effective_count({}) == doctest::Approx(1.0));
  CHECK(effective_count({4.9}) == doctest::Approx(1.0));
  CHECK(effective_count({2.4, 2.4, 2.4, 2.4}) == doctest::Approx(4.0));
  // One near enemy plus ten at the edge barely counts as a crowd.
  std::vector<double> c{4.9}; for (int i = 0; i < 10; ++i) c.push_back(0.1);
  CHECK(effective_count(c) == doctest::Approx(1.44).epsilon(0.02));
}

TEST_CASE("compression: nothing at or below the pivot is ever boosted; a field under the cap is untouched") {
  CHECK(CompressParams{}.cap == 5.0); CHECK(CompressParams{}.pivot == 0.5);   // the by-ear defaults of 2026-09-20
  CHECK(compress_gain(0.1, 0.1, 3.0) == 1.0f);
  CHECK(compress_gain(0.05, 0.1, 3.0) == 1.0f);
  CHECK(compress_gain(4.9, 0.1, 1.0) == 1.0f);
  CHECK(solve_ratio({4.9, 4.9, 4.9, 4.9}, 0.1, 25.0) == 1.0);   // 19.6 < 25: no compression
}

TEST_CASE("compression: a lone near enemy and a pack of four are untouched at cap 25; ten at 2u are fitted to it") {
  FieldParams p;
  double near = contrib(2.0f, p);   // ~4.9
  CHECK(near == doctest::Approx(4.96).epsilon(0.02));
  LevelCompressor lc; lc.params().cap = 25.0; lc.params().pivot = 0.1;   // hard cap
  CHECK(lc.update({near}, 0.0) == 1.0);
  CHECK(lc.update({near, near, near, near}, 0.1) == 1.0);
  std::vector<double> ten(10, near);   // B ~ 49.6
  double r = lc.update(ten, 100.0);     // the slew has long since settled to the target (dt 100 s)
  CHECK(r > 1.0);
  CHECK(compressed_power(ten, 0.1, r) == doctest::Approx(25.0).epsilon(0.001));
  double loss = db(lc.gain_for(near));
  CHECK(loss == doctest::Approx(-3.0).epsilon(0.05));   // power halved -> each pulse -3 dB
  // Ordering kept: a compressed near thing is still louder than an untouched far one.
  CHECK(lc.gain_for(near) * std::sqrt(near) > std::sqrt(0.1));
}

TEST_CASE("compression: the biggest contributors give the most") {
  std::vector<double> c{4.9, 4.9, 4.9, 4.9, 4.9, 4.9, 1.0, 1.0, 0.1, 0.1};   // B = 31.6
  double r = solve_ratio(c, 0.1, 25.0);
  CHECK(compress_gain(4.9, 0.1, r) < compress_gain(1.0, 0.1, r));
  CHECK(compress_gain(0.1, 0.1, r) == 1.0f);
}

TEST_CASE("compression: a soft output ratio lets the field exceed the cap by (B - cap) / ratio") {
  std::vector<double> ten(10, 4.9);   // B = 49
  LevelCompressor lc; lc.params().cap = 25.0; lc.params().pivot = 0.1; lc.params().ratio = 4.0;   // target 25 + 24/4 = 31
  double r = lc.update(ten, 0.0);
  CHECK(compressed_power(ten, 0.1, r) == doctest::Approx(31.0).epsilon(0.001));
}

TEST_CASE("compression: twenty at the edge change nothing (they are at the pivot and under the cap)") {
  std::vector<double> far(20, 0.1);
  LevelCompressor lc;
  CHECK(lc.update(far, 0.0) == 1.0);
  CHECK(lc.gain_for(0.1) == 1.0f);
}

TEST_CASE("compression: the live pack of 2026-09-20 at the defaults: near scarabs lose most, the 8.8 u one nothing") {
  // Nine Korvan scarabs at 2.6..8.8 u, contributions read off /sonar; the settings the user kept by ear.
  std::vector<double> pack{3.08, 1.69, 1.46, 1.34, 1.10, 0.96, 0.96, 0.74, 0.53};   // B = 11.9
  LevelCompressor lc;
  double r = lc.update(pack, 0.0);
  CHECK(compressed_power(pack, 0.5, r) == doctest::Approx(5.0).epsilon(0.001));
  CHECK(db(lc.gain_for(3.08)) == doctest::Approx(-6.9).epsilon(0.05));
  CHECK(db(lc.gain_for(1.34)) == doctest::Approx(-3.8).epsilon(0.05));
  CHECK(db(lc.gain_for(0.53)) > -0.3);   // just over the pivot: as good as untouched (-0.2 dB)
  CHECK(lc.gain_for(0.40) == 1.0f);      // under the pivot: untouched
  CHECK(lc.update({3.08}, 10.0) == doctest::Approx(1.0).epsilon(0.001));   // a lone scarab is under the cap (slew settled)
}

TEST_CASE("compression: the ratio slews toward its target and snaps on the first frame") {
  LevelCompressor lc;   // slew 0.25 s
  std::vector<double> ten(10, 4.9);
  double r0 = lc.update(ten, 0.0);
  CHECK(r0 > 1.0);                                          // first frame: no history, take the target
  double r1 = lc.update({}, 0.01);
  CHECK(r1 < r0); CHECK(r1 > 1.0);                          // the pack vanishes: heads back to 1
  CHECK(lc.update({}, 5.0) == doctest::Approx(1.0).epsilon(0.001));   // long after: settled
  lc.reset();
  CHECK(lc.ratio() == 1.0);
}
