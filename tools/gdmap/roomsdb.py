"""The rooms database (assets/rooms.db): segmentation results + authored titles/descriptions, written by the
tools, read-only for the mod. Schema is in docs/rooms.md. Room keys are coordinate anchors
("<region>:<x>:<z>") so authored rows survive a re-segmentation: a rerun keeps title/body/subregion for any
room whose key still exists and marks vanished keys `orphan`."""
from __future__ import annotations

import json
import sqlite3
import struct
from dataclasses import asdict

import numpy as np

SCHEMA_VERSION = 1
SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS regions(
  key TEXT PRIMARY KEY, name TEXT, location_record TEXT, chunks TEXT, params TEXT,
  algo_version INTEGER, signature TEXT, stale INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS grids(
  region_key TEXT PRIMARY KEY, x0 REAL, z0 REAL, w INTEGER, h INTEGER, cell REAL, labels BLOB, label_keys TEXT);
CREATE TABLE IF NOT EXISTS rooms(
  key TEXT PRIMARY KEY, region_key TEXT, subregion_key TEXT, anchor_x REAL, anchor_z REAL, cls TEXT,
  area REAL, walk REAL, bbox TEXT, island INTEGER, title TEXT, body TEXT, status TEXT);
CREATE TABLE IF NOT EXISTS subregions(key TEXT PRIMARY KEY, region_key TEXT, name TEXT, summary TEXT);
CREATE TABLE IF NOT EXISTS exits(
  region_key TEXT, room_a TEXT, room_b TEXT, x REAL, z REAL, width REAL, cut INTEGER);
CREATE TABLE IF NOT EXISTS shots(room_key TEXT, path TEXT, cam TEXT, sha TEXT);
CREATE TABLE IF NOT EXISTS terrain_types(record TEXT PRIMARY KEY, class TEXT, surface TEXT);
CREATE TABLE IF NOT EXISTS coverage(region_key TEXT PRIMARY KEY, cells_total INTEGER, cells_described INTEGER);
CREATE INDEX IF NOT EXISTS rooms_region ON rooms(region_key);
CREATE INDEX IF NOT EXISTS exits_region ON exits(region_key);
"""


def room_key(region_key: str, anchor: tuple[float, float]) -> str:
    return f"{region_key}:{round(anchor[0])}:{round(anchor[1])}"


def rle_encode(labels: np.ndarray) -> bytes:
    """Row-major runs of (int16 value, uint16 length); the mod decodes the same format (core/rooms_model)."""
    flat = labels.astype(np.int16).ravel()
    if flat.size == 0:
        return b""
    change = np.flatnonzero(np.diff(flat)) + 1
    starts = np.concatenate([[0], change])
    ends = np.concatenate([change, [flat.size]])
    out = []
    for s, e in zip(starts, ends):
        v = int(flat[s]); n = int(e - s)
        while n > 0:
            k = min(n, 65535)
            out.append(struct.pack("<hH", v, k))
            n -= k
    return b"".join(out)


def rle_decode(blob: bytes, h: int, w: int) -> np.ndarray:
    a = np.frombuffer(blob, dtype=np.dtype([("v", "<i2"), ("n", "<u2")]))
    return np.repeat(a["v"], a["n"]).reshape(h, w)


class RoomsDb:
    def __init__(self, path: str = "assets/rooms.db"):
        self.path = path
        self.c = sqlite3.connect(path)
        self.c.executescript(SCHEMA)
        self.c.execute("INSERT OR IGNORE INTO meta(key, value) VALUES('schema', ?)", (str(SCHEMA_VERSION),))
        self.c.commit()

    # ---- params ----
    def params(self, region_key: str) -> dict | None:
        row = self.c.execute("SELECT params FROM regions WHERE key=?", (region_key,)).fetchone()
        return json.loads(row[0]) if row and row[0] else None

    def set_params(self, region_key: str, params: dict) -> None:
        self.c.execute("INSERT INTO regions(key, params) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET params=excluded.params",
                       (region_key, json.dumps(params)))
        self.c.commit()

    def set_name(self, region_key: str, name: str) -> None:
        self.c.execute("INSERT INTO regions(key, name) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET name=excluded.name",
                       (region_key, name))
        self.c.commit()

    # ---- segmentation ----
    def write_segmentation(self, region_key: str, name: str | None, location: str, chunks: list[str], params,
                           signature: str, algo_version: int, grid, seg) -> dict:
        """Store grid/rooms/exits for a region. Returns counts incl. kept/orphaned authored rows."""
        c = self.c
        old_name = c.execute("SELECT name FROM regions WHERE key=?", (region_key,)).fetchone()
        name = name or (old_name[0] if old_name and old_name[0] else region_key)
        c.execute("""INSERT INTO regions(key, name, location_record, chunks, params, algo_version, signature, stale)
                     VALUES(?,?,?,?,?,?,?,0) ON CONFLICT(key) DO UPDATE SET name=excluded.name,
                     location_record=excluded.location_record, chunks=excluded.chunks, params=excluded.params,
                     algo_version=excluded.algo_version, signature=excluded.signature, stale=0""",
                  (region_key, name, location, json.dumps(chunks), json.dumps(asdict(params)), algo_version, signature))
        keys = [room_key(region_key, r.anchor) for r in seg.rooms]
        # duplicate anchors (two rooms rounding to one cell) get a suffix so keys stay unique
        seen = {}
        for i, k in enumerate(keys):
            if k in seen:
                keys[i] = f"{k}#{seen[k]}"
            seen[k] = seen.get(k, 0) + 1
        by_label = {r.id: keys[i] for i, r in enumerate(seg.rooms)}
        n = int(seg.labels.max()) + 1
        label_keys = [by_label.get(i, "") for i in range(n)]
        lab16 = seg.labels.astype(np.int16)
        c.execute("""INSERT INTO grids(region_key, x0, z0, w, h, cell, labels, label_keys) VALUES(?,?,?,?,?,?,?,?)
                     ON CONFLICT(region_key) DO UPDATE SET x0=excluded.x0, z0=excluded.z0, w=excluded.w, h=excluded.h,
                     cell=excluded.cell, labels=excluded.labels, label_keys=excluded.label_keys""",
                  (region_key, grid.x0, grid.z0, lab16.shape[1], lab16.shape[0], 0.25,
                   rle_encode(lab16), json.dumps(label_keys)))
        existing = {row[0]: row for row in c.execute("SELECT key, title, body, subregion_key, status FROM rooms WHERE region_key=?", (region_key,))}
        kept = 0
        for i, r in enumerate(seg.rooms):
            k = keys[i]
            old = existing.pop(k, None)
            if old:
                kept += 1
                status = old[4] if old[4] not in (None, "orphan", "stale") else "unseen"
                c.execute("""UPDATE rooms SET cls=?, area=?, walk=?, bbox=?, island=?, anchor_x=?, anchor_z=?, status=? WHERE key=?""",
                          (r.cls, r.area, r.walk, json.dumps(r.bbox), int(r.island), r.anchor[0], r.anchor[1], status, k))
            else:
                c.execute("""INSERT INTO rooms(key, region_key, subregion_key, anchor_x, anchor_z, cls, area, walk, bbox, island, title, body, status)
                             VALUES(?,?,?,?,?,?,?,?,?,?,?,?,'unseen')""",
                          (k, region_key, None, r.anchor[0], r.anchor[1], r.cls, r.area, r.walk, json.dumps(r.bbox), int(r.island), None, None))
        for k in existing:
            c.execute("UPDATE rooms SET status='orphan' WHERE key=?", (k,))
        c.execute("DELETE FROM exits WHERE region_key=?", (region_key,))
        c.executemany("INSERT INTO exits(region_key, room_a, room_b, x, z, width, cut) VALUES(?,?,?,?,?,?,?)",
                      [(region_key, by_label[e.a], by_label[e.b], e.x, e.z, e.width, int(e.cut)) for e in seg.exits
                       if e.a in by_label and e.b in by_label])
        c.commit()
        return {"rooms": len(seg.rooms), "kept": kept, "orphaned": len(existing), "exits": len(seg.exits)}

    # ---- reads ----
    def status(self) -> list[dict]:
        out = []
        for key, name, chunks, sig, stale in self.c.execute("SELECT key, name, chunks, signature, stale FROM regions ORDER BY key"):
            counts = dict(self.c.execute("SELECT status, COUNT(*) FROM rooms WHERE region_key=? GROUP BY status", (key,)).fetchall())
            out.append({"key": key, "name": name, "chunks": len(json.loads(chunks)) if chunks else 0, "signature": sig,
                        "stale": bool(stale), "rooms": counts})
        return out

    def rooms(self, region_key: str) -> list[dict]:
        cur = self.c.execute("SELECT * FROM rooms WHERE region_key=? ORDER BY anchor_z, anchor_x", (region_key,))
        cols = [d[0] for d in cur.description]
        return [dict(zip(cols, row)) for row in cur]

    def grid(self, region_key: str):
        row = self.c.execute("SELECT x0, z0, w, h, cell, labels, label_keys FROM grids WHERE region_key=?", (region_key,)).fetchone()
        if not row:
            return None
        x0, z0, w, h, cell, blob, label_keys = row
        labels = rle_decode(blob, h, w)
        return x0, z0, cell, labels, json.loads(label_keys)

    def mark_stale(self, region_key: str, signature: str) -> bool:
        row = self.c.execute("SELECT signature FROM regions WHERE key=?", (region_key,)).fetchone()
        stale = bool(row and row[0] and row[0] != signature)
        if stale:
            self.c.execute("UPDATE regions SET stale=1 WHERE key=?", (region_key,))
            self.c.execute("UPDATE rooms SET status='stale' WHERE region_key=? AND status IN ('described','verified','shot')", (region_key,))
            self.c.commit()
        return stale
