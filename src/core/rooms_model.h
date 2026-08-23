#pragma once
// Engine-free model of the rooms data (assets/rooms.db, written by tools/rooms.py): a run-length-encoded
// label grid per region looked up by world position, a dwell-gated "current room" (hysteresis), and the
// exit cycling state. Bearings/distances are computed by the caller with world::clock_hour.
#include <cstdint>
#include <string>
#include <vector>

namespace gd::core::rooms {

struct LabelGrid {
  double x0 = 0, z0 = 0, cell = 0.25;
  int w = 0, h = 0;
  std::vector<int16_t> labels;   // row-major [row = z][col = x], -1 = unwalkable

  bool decode_rle(const uint8_t* blob, size_t size, int width, int height);  // (int16 value, uint16 run)*
  int at(int col, int row) const;                                              // -1 outside
  // The label under (x, z), searching rings of radius 0..ring cells for the nearest labelled cell
  // (wotr's RoomAt: a point just off the mesh still resolves to the room beside it).
  int label_at(double x, double z, int ring = 2) const;
};

// The current room changes immediately once the player has been in it for settle_ms (a genuine move);
// within the first settle_ms after a change (the back-and-forth on a boundary) a candidate must persist
// dwell_ms first. Returns true on the frame the current room changes. Off-grid observations (-1) are ignored.
struct Hysteresis {
  int dwell_ms = 400;
  int settle_ms = 1000;
  int current = -1;
  int candidate = -1;
  int candidate_since = 0;
  int entered = 0;           // when `current` became current
  bool update(int observed, int now_ms);
  void reset() { current = candidate = -1; candidate_since = entered = 0; }
};

// "i of n" cycling with wrap; next(count, +1/-1) from the last index (wotr's continue-from rule).
struct Cycle {
  int last = -1;
  int next(int count, int dir);
  void reset() { last = -1; }
};

}  // namespace gd::core::rooms
