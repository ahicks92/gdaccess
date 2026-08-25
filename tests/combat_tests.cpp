#include <doctest/doctest.h>
#include "core/combat_coalesce.h"
#include "core/combat_text.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "core/threshold_watcher.h"

using gd::core::CombatCoalescer;
using gd::core::MessageBuilder;
using gd::core::ThresholdWatcher;
using gd::core::parse_combat_text;

TEST_CASE("combat text: plain numbers") {
  auto t = parse_combat_text("123", false);
  CHECK(t.is_number); CHECK(t.number == "123"); CHECK(t.amount == doctest::Approx(123)); CHECK_FALSE(t.crit);
  t = parse_combat_text("1,234", false);
  CHECK(t.is_number); CHECK(t.number == "1234"); CHECK(t.amount == doctest::Approx(1234));
  t = parse_combat_text("  42 ", true);
  CHECK(t.is_number); CHECK(t.number == "42"); CHECK(t.crit);
}
TEST_CASE("combat text: crit multiplier suffix is stripped and means crit") {
  auto t = parse_combat_text("456 (x1.50)", false);
  CHECK(t.is_number); CHECK(t.number == "456"); CHECK(t.crit);
}
TEST_CASE("combat text: words pass through verbatim") {
  auto t = parse_combat_text("Miss", false);
  CHECK_FALSE(t.is_number); CHECK(t.word == "Miss"); CHECK_FALSE(t.crit);
  t = parse_combat_text("Esquiv\xc3\xa9", false);  // non-ASCII stays a word
  CHECK_FALSE(t.is_number); CHECK(t.word == "Esquiv\xc3\xa9");
  t = parse_combat_text("", false);
  CHECK_FALSE(t.is_number); CHECK(t.word.empty());
}
TEST_CASE("combat strings") {
  MessageBuilder a; gd::strings::push_combat_hit(a, "456", true); CHECK(a.build() == "456 crit");
  MessageBuilder b; gd::strings::push_combat_hit(b, "7", false); CHECK(b.build() == "7");
  MessageBuilder c; gd::strings::push_health_percent(c, 70); CHECK(c.build() == "health 70 percent");
  MessageBuilder d; gd::strings::push_vitals(d, 250.0, 250.0f, 100.0f, 120.0f); CHECK(d.build() == "health 250 of 250, energy 100 of 120");
}

static CombatCoalescer::In hit(double amount, float x, float z, double t, bool crit = false) {
  return {true, amount, crit, {}, {}, x, z, 0.3f, 0.7f, t};
}
static CombatCoalescer::In effect(std::string tag, float x, float z, double t) {
  return {false, 0, false, {}, {std::move(tag)}, x, z, 0.3f, 0.7f, t};
}
TEST_CASE("coalescer merges same-place numbers inside the window") {
  CombatCoalescer c;
  c.push(hit(10, 5.2f, 7.1f, 0.00));
  c.push(hit(15, 4.9f, 7.3f, 0.05));
  CHECK(c.flush(0.10).empty());         // window still open
  auto out = c.flush(0.20);
  REQUIRE(out.size() == 1);
  CHECK(out[0].is_number); CHECK(out[0].amount == doctest::Approx(25)); CHECK(c.merged() == 1);
}
TEST_CASE("coalescer keeps separate what must stay separate") {
  CombatCoalescer c;
  c.set_max_per_flush(10);
  c.push(hit(10, 5, 7, 0.0));
  c.push(hit(10, 5, 7, 0.3));           // later than the window: its own bucket
  c.push(hit(10, 5, 7, 0.31, true));    // crit: its own bucket
  c.push(hit(10, 9, 9, 0.31));          // elsewhere
  c.push({false, 0, false, "Miss", {}, 5, 7, 0, 1, 0.31});
  auto out = c.flush(1.0);
  CHECK(out.size() == 5);
  CHECK_FALSE(out[0].is_number); CHECK(out[0].word == "Miss");  // words first, never merged
  CHECK(c.merged() == 0);
}
TEST_CASE("coalescer disabled passes everything through, cap drops the surplus") {
  CombatCoalescer c;
  c.set_enabled(false);
  c.set_max_per_flush(2);
  for (int i = 0; i < 5; ++i) c.push(hit(1, 0, 0, 0.0));
  auto out = c.flush(0.0);
  CHECK(out.size() == 2); CHECK(c.dropped() == 3); CHECK(c.merged() == 0);
}

TEST_CASE("coalescer: a debuff at the same place rides the number bucket") {
  CombatCoalescer c;
  c.push(hit(12, 5.0f, 7.0f, 0.00));
  c.push(effect("frozen", 5.1f, 6.9f, 0.03));   // same place, inside the window
  auto out = c.flush(0.20);
  REQUIRE(out.size() == 1);
  CHECK(out[0].is_number); CHECK(out[0].amount == doctest::Approx(12));
  REQUIRE(out[0].tags.size() == 1); CHECK(out[0].tags[0] == "frozen");
}
TEST_CASE("coalescer: a lone debuff flushes on its own") {
  CombatCoalescer c;
  c.push(effect("stunned", 5, 7, 0.0));
  auto out = c.flush(0.20);
  REQUIRE(out.size() == 1);
  CHECK_FALSE(out[0].is_number); CHECK(out[0].word.empty());
  REQUIRE(out[0].tags.size() == 1); CHECK(out[0].tags[0] == "stunned");
}
TEST_CASE("coalescer: a debuff adopts an effect-only bucket then the number joins") {
  CombatCoalescer c;
  c.push(effect("burning", 5, 7, 0.00));        // effect first
  c.push(hit(8, 5, 7, 0.02));                   // number lands on the same place
  auto out = c.flush(0.20);
  REQUIRE(out.size() == 1);
  CHECK(out[0].is_number); CHECK(out[0].amount == doctest::Approx(8));
  REQUIRE(out[0].tags.size() == 1); CHECK(out[0].tags[0] == "burning");
}
TEST_CASE("threshold watcher fires once per decade in both directions") {
  ThresholdWatcher w;
  int pct = -1;
  CHECK_FALSE(w.update(1.0, pct));       // first sample only records
  CHECK(w.update(0.95, pct)); CHECK(pct == 90);   // full is its own bucket: the first scratch says "90"
  CHECK_FALSE(w.update(0.91, pct));
  CHECK(w.update(0.89, pct)); CHECK(pct == 80);
  CHECK_FALSE(w.update(0.81, pct));
  CHECK(w.update(0.799, pct)); CHECK(pct == 70);
  CHECK_FALSE(w.update(0.701, pct));     // jitter inside the decade
  CHECK_FALSE(w.update(0.72, pct));
  CHECK(w.update(0.25, pct)); CHECK(pct == 20);   // a big hit skips decades: one announcement, the landing
  CHECK(w.update(0.05, pct)); CHECK(pct == 0);
  CHECK(w.update(0.31, pct)); CHECK(pct == 30);   // recovery
  CHECK(w.update(1.0, pct)); CHECK(pct == 100);
  w.reset();
  CHECK_FALSE(w.update(0.5, pct));
  CHECK(w.update(0.49, pct)); CHECK(pct == 40);
}
