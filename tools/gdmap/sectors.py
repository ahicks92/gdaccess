"""The .lvl 'sector' layers: painted per-cell index grids naming areas for the HUD (and the other
per-cell layer families -- ambient, climate, ... -- share the same container).

Layout inside a chunk body (validated against 643 live `Engine::GetAreaNameTag` reads, 100%,
2026-08-31): `[u32 1][u32 ntab]` then `ntab` tables, each `[u32 n]` + n entries of
`[u8 editor-id][16-byte GUID]` (ids are arbitrary, not contiguous -- entries can be deleted in the
editor), then `[u32 w][u32 h]` and `w*h*ntab` bytes of per-cell ids, **x-major** (cell (cx, cz) is at
`(cx*h + cz)*ntab`), one byte per table per cell, 0 = none. The grid spans the chunk's footprint:
local x in [0, 128) maps to `cx = int(lx * w / 128)` (w is 127 for a 128-unit chunk). **Table 1 is the
area-name layer**; its GUIDs resolve through the map header's unique-entities table (name, GUID, two
editor RGBA colours, localization tag) to `tagMap*` tags in Text_EN. Underground chunks use the same
chunk-local mapping via their `GetOffsetFromWorld`.
"""
from __future__ import annotations

import re
import struct

AREA_TABLE = 1


def header_entities(head: bytes) -> dict[bytes, tuple[str | None, str]]:
    """The map header's unique-entities table: {guid: (editor name, tag)} for every tagged entity.
    Entry layout: [u32 len][editor name][guid 16][8 floats: two RGBA][u32 len][tag][3 x u32]."""
    out = {}
    for m in re.finditer(rb"tag[A-Za-z0-9_]{3,40}", head):
        s = m.start()
        if struct.unpack_from("<I", head, s - 4)[0] != len(m.group()):
            continue
        gpos = s - 4 - 32 - 16
        guid = head[gpos:gpos + 16]
        name = None
        for L in range(1, 80):
            if gpos - 4 - L < 0:
                break
            if struct.unpack_from("<I", head, gpos - 4 - L)[0] == L:
                cand = head[gpos - L:gpos]
                if all(0x20 <= c < 0x7f for c in cand):
                    name = cand.decode()
                    break
        out[guid] = (name, m.group().decode())
    return out


class Sectors:
    def __init__(self, tables, w, h, ntab, cells):
        self.tables, self.w, self.h, self.ntab, self.cells = tables, w, h, ntab, cells

    def guid_at(self, lx: float, lz: float, table: int = AREA_TABLE) -> bytes | None:
        """The GUID painted at chunk-local (lx, lz) in the given layer, or None."""
        cx, cz = int(lx * self.w / 128.0), int(lz * self.h / 128.0)
        if not (0 <= cx < self.w and 0 <= cz < self.h) or table >= self.ntab:
            return None
        idx = self.cells[(cx * self.h + cz) * self.ntab + table]
        return self.tables[table].get(idx) if idx else None


def parse_sectors(body: bytes) -> Sectors | None:
    """Locate + parse the sector section of a chunk body (self-verifying scan)."""
    i = 0
    while True:
        i = body.find(b"\x01\x00\x00\x00", i + 1)
        if i < 0 or i + 8 > len(body):
            return None
        ntab = struct.unpack_from("<I", body, i + 4)[0]
        if not (1 <= ntab <= 32):
            continue
        o, tables, good = i + 8, [], True
        for _ in range(ntab):
            if o + 4 > len(body):
                good = False
                break
            n = struct.unpack_from("<I", body, o)[0]
            if n > 48 or o + 4 + 17 * n > len(body):
                good = False
                break
            entries = {}
            for j in range(n):
                idx = body[o + 4 + 17 * j]
                if idx < 1 or idx in entries:
                    good = False
                    break
                entries[idx] = body[o + 5 + 17 * j:o + 5 + 17 * j + 16]
            if not good:
                break
            tables.append(entries)
            o += 4 + 17 * n
        if not good or len(tables) != ntab:
            continue
        if o + 8 > len(body):
            continue
        w, h = struct.unpack_from("<II", body, o)
        if not (0 < w <= 512 and 0 < h <= 512) or o + 8 + w * h * ntab > len(body):
            continue
        cells = body[o + 8:o + 8 + w * h * ntab]
        bad, total = 0, min(2048, w * h)
        for c in range(total):
            for p in range(ntab):
                v = cells[c * ntab + p]
                if v and v not in tables[p]:
                    bad += 1
        if bad > total * ntab * 0.02:
            continue
        return Sectors(tables, w, h, ntab, cells)
