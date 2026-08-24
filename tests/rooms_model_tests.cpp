#include <doctest/doctest.h>
#include "core/rooms_model.h"

using gd::core::rooms::Cycle;
using gd::core::rooms::Hysteresis;
using gd::core::rooms::LabelGrid;

namespace {
std::vector<uint8_t> rle(std::initializer_list<std::pair<int, int>> runs) {
  std::vector<uint8_t> out;
  for (auto [vi, ni] : runs) {
    int16_t v = (int16_t)vi; uint16_t n = (uint16_t)ni;
    out.push_back((uint8_t)(v & 0xff)); out.push_back((uint8_t)((v >> 8) & 0xff));
    out.push_back((uint8_t)(n & 0xff)); out.push_back((uint8_t)((n >> 8) & 0xff));
  }
  return out;
}
}  // namespace

TEST_CASE("rle decode fills the grid row-major and rejects short or long blobs") {
  LabelGrid g;
  auto blob = rle({{-1, 2}, {0, 3}, {1, 1}});   // 2x3 grid: row0 = -1 -1 0, row1 = 0 0 1
  REQUIRE(g.decode_rle(blob.data(), blob.size(), 3, 2));
  CHECK(g.at(0, 0) == -1); CHECK(g.at(2, 0) == 0); CHECK(g.at(0, 1) == 0); CHECK(g.at(2, 1) == 1);
  CHECK(g.at(3, 0) == -1); CHECK(g.at(-1, 0) == -1);
  auto shortb = rle({{0, 5}});
  CHECK_FALSE(g.decode_rle(shortb.data(), shortb.size(), 3, 2));
  auto longb = rle({{0, 7}});
  CHECK_FALSE(g.decode_rle(longb.data(), longb.size(), 3, 2));
}

TEST_CASE("label_at maps world coordinates through origin and cell, with a ring search off the mesh") {
  LabelGrid g;
  g.x0 = 10; g.z0 = -20; g.cell = 0.5;
  auto blob = rle({{-1, 4}, {-1, 1}, {7, 2}, {-1, 1}, {-1, 4}});   // 4x3: the middle row has room 7 at cols 1..2
  REQUIRE(g.decode_rle(blob.data(), blob.size(), 4, 3));
  CHECK(g.label_at(10.75, -19.5) == 7);      // col 1, row 1
  CHECK(g.label_at(10.25, -19.5, 0) == -1);  // col 0: unwalkable, no ring
  CHECK(g.label_at(10.25, -19.5, 1) == 7);   // one ring away
  CHECK(g.label_at(10.25, -20.0, 1) == 7);   // diagonal neighbour counts in the ring
  CHECK(g.label_at(0, 0) == -1);             // far outside
}

TEST_CASE("stacked cells resolve by player height: base room below, overlay room above, unlabeled overlay is honest") {
  LabelGrid g;
  g.x0 = 0; g.z0 = 0; g.cell = 1.0;
  auto labels = rle({{3, 4}});               // 2x2, all room 3 on the base plane
  REQUIRE(g.decode_rle(labels.data(), labels.size(), 2, 2));
  auto heights = rle({{20, 4}});             // base floor at 2.0 everywhere (decimeters)
  REQUIRE(g.decode_heights(heights.data(), heights.size()));
  // overlay at cell (col 1, row 0): a bridge at y 8.0 belonging to room 9; at (col 0, row 1) an
  // enclosed upper floor no room owns (label -1)
  std::vector<uint8_t> ob;
  auto put16 = [&](int v) { ob.push_back((uint8_t)(v & 0xff)); ob.push_back((uint8_t)((v >> 8) & 0xff)); };
  put16(0); put16(1); put16(80); put16(9);
  put16(1); put16(0); put16(80); put16(-1 & 0xffff);
  REQUIRE(g.decode_overlays(ob.data(), ob.size()));
  CHECK(g.label_at(1.5, 0.5, 2.1, 0) == 3);   // under the bridge: the base room
  CHECK(g.label_at(1.5, 0.5, 7.8, 0) == 9);   // on the bridge: the overlay room
  CHECK(g.label_at(1.5, 0.5, 0) == 3);        // no y given: base behaviour unchanged
  CHECK(g.label_at(0.5, 1.5, 7.8, 0) == -1);  // enclosed upper floor: no room, not the room below
  CHECK(g.label_at(0.5, 1.5, 2.1, 0) == 3);   // but standing below it is still the base room
  CHECK(g.label_at(0.5, 0.5, 7.8, 0) == 3);   // an unstacked cell ignores y entirely
}

TEST_CASE("hysteresis: first room immediate, a settled room changes at once, boundary flapping waits for the dwell") {
  Hysteresis h; h.dwell_ms = 400; h.settle_ms = 1000;
  CHECK(h.update(3, 0)); CHECK(h.current == 3);
  CHECK_FALSE(h.update(4, 100));            // within the settle window: candidate 4 since 100
  CHECK_FALSE(h.update(3, 200));            // back in 3: candidate cleared
  CHECK_FALSE(h.update(4, 300));            // candidate 4 since 300
  CHECK_FALSE(h.update(4, 600));
  CHECK(h.update(4, 700)); CHECK(h.current == 4);          // dwell met
  CHECK_FALSE(h.update(-1, 800)); CHECK(h.current == 4);   // off the grid keeps the room
  CHECK(h.update(5, 1800)); CHECK(h.current == 5);         // settled for 1.1 s: immediate
  CHECK_FALSE(h.update(4, 1900));                          // just changed: flapping back waits again
  h.reset(); CHECK(h.current == -1);
}

TEST_CASE("path_is_direct: a route inside the two rooms is direct; one detouring through a third is not") {
  // 3x3, cell 1, origin 0. Left column + top/bottom of the middle = room 0 (A); right column = room 1 (B);
  // the centre cell (1,1) = room 7 (C, a third room).  Row-major: 0 0 1 / 0 7 1 / 0 0 1.
  LabelGrid g;
  g.x0 = 0; g.z0 = 0; g.cell = 1.0;
  auto blob = rle({{0, 2}, {1, 1}, {0, 1}, {7, 1}, {1, 1}, {0, 2}, {1, 1}});
  REQUIRE(g.decode_rle(blob.data(), blob.size(), 3, 3));
  // Cell centres are (col+0.5, row+0.5). A route along the top row A->A->B never touches C.
  std::vector<std::array<double, 3>> direct = {{0.5, 0, 0.5}, {1.5, 0, 0.5}, {2.5, 0, 0.5}};
  CHECK(g.path_is_direct(direct, 0, 1, /*ring*/ 0, /*tol*/ 0.5));
  // A route through the middle row A->C->B passes through the centre C.
  std::vector<std::array<double, 3>> detour = {{0.5, 0, 1.5}, {1.5, 0, 1.5}, {2.5, 0, 1.5}};
  CHECK_FALSE(g.path_is_direct(detour, 0, 1, 0, 0.5));   // a 1-unit stretch in C exceeds tol 0.5
  CHECK(g.path_is_direct(detour, 0, 1, 0, 2.0));         // the same graze is under tol 2.0 -> tolerated
}

TEST_CASE("path_is_direct: a long stretch inside a third room is caught mid-segment; gaps and short paths pass") {
  // 5x1, cell 1: A C C C B.  A single long segment crosses all three C cells.
  LabelGrid g;
  g.x0 = 0; g.z0 = 0; g.cell = 1.0;
  auto blob = rle({{0, 1}, {7, 3}, {1, 1}});
  REQUIRE(g.decode_rle(blob.data(), blob.size(), 5, 1));
  std::vector<std::array<double, 3>> across = {{0.5, 0, 0.5}, {4.5, 0, 0.5}};   // one segment A..B over C,C,C
  CHECK_FALSE(g.path_is_direct(across, 0, 1, 0, 2.0));   // ~3 units inside C, sampled along the segment
  // An unwalkable/off-grid gap between the rooms is not a "third room": B at col 4, unlabeled between.
  LabelGrid gap;
  gap.x0 = 0; gap.z0 = 0; gap.cell = 1.0;
  auto gb = rle({{0, 1}, {-1, 1}, {1, 1}});   // A . B
  REQUIRE(gap.decode_rle(gb.data(), gb.size(), 3, 1));
  std::vector<std::array<double, 3>> over_gap = {{0.5, 0, 0.5}, {1.5, 0, 0.5}, {2.5, 0, 0.5}};
  CHECK(gap.path_is_direct(over_gap, 0, 1, 0, 0.5));   // the -1 cell is ignored, not a detour
  CHECK(gap.path_is_direct({}, 0, 1));                 // no corridor -> fail open (direct)
  CHECK(gap.path_is_direct({{0.5, 0, 0.5}}, 0, 1));    // single point -> fail open
}

TEST_CASE("cycle wraps both ways and starts from the nearest") {
  Cycle c;
  CHECK(c.next(3, +1) == 0); CHECK(c.next(3, +1) == 1); CHECK(c.next(3, +1) == 2); CHECK(c.next(3, +1) == 0);
  c.reset(); CHECK(c.next(3, -1) == 2); CHECK(c.next(3, -1) == 1);
  CHECK(c.next(0, +1) == -1);
  c.reset(); c.last = 5; CHECK(c.next(2, +1) == 0);   // count shrank: restart
}
