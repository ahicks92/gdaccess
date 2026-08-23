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

TEST_CASE("cycle wraps both ways and starts from the nearest") {
  Cycle c;
  CHECK(c.next(3, +1) == 0); CHECK(c.next(3, +1) == 1); CHECK(c.next(3, +1) == 2); CHECK(c.next(3, +1) == 0);
  c.reset(); CHECK(c.next(3, -1) == 2); CHECK(c.next(3, -1) == 1);
  CHECK(c.next(0, +1) == -1);
  c.reset(); c.last = 5; CHECK(c.next(2, +1) == 0);   // count shrank: restart
}
