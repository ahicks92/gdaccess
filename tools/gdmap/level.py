"""A region's level body ("LVL" blob). Two things are read from it:

- Detour tile-cache layers (dtTileCacheLayerHeader v1, magic "DTLR"): 32x32-unit tiles at 128x128 cells
  (0.25 units/cell), each a length-prefixed 49208-byte blob stored uncompressed: 52-byte header, then
  heights[128*128], areas[128*128], cons[128*128]. area 0 = unwalkable; 1/2 are the engine's checkerboard
  (polygon splitting), not surface classes. Tile (tx, ty) covers region-local x in [tx*32, tx*32+32),
  z in [ty*32, ty*32+32); tile indices can be negative. Height = bmin.y + heights*0.2.
  Quest gates (doors, barricades) are runtime obstacles on top of this bake, so the bake is "everything
  walkable regardless of quest state".
- Terrain texture layers: a length-prefixed records/terraintextures/*.dbr name followed by a 128x128 u8
  opacity mask at 1 unit/sample (the first, base layer has no mask). Roads are painted layers (gravel,
  cobbles, flagstone...); the class is decided per record by tools, not by the game's surfaceType.
Measured 2026-08-22 on Region0A001; the exact framing of the terrain block (count prefix, base layer) is
inferred from byte gaps, see the note in the function."""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

import numpy as np

CELL = 0.25
TILE_CELLS = 128
TILE_UNITS = 32.0
CH = 0.2
TERRAIN_SAMPLE = 1.0
TERRAIN_SAMPLES = 128


@dataclass
class Tile:
    tx: int
    ty: int
    layer: int
    bmin: tuple[float, float, float]
    bmax: tuple[float, float, float]
    heights: np.ndarray   # (128, 128) u8, [row = z cell][col = x cell]
    areas: np.ndarray
    cons: np.ndarray


def parse_tiles(body: bytes) -> list[Tile]:
    tiles = []
    for m in re.finditer(b"RLTD", body):
        o = m.start()
        magic, ver, tx, ty, tl = struct.unpack_from("<4sIiii", body, o)
        if ver != 1:
            continue
        bmin = struct.unpack_from("<3f", body, o + 20)
        bmax = struct.unpack_from("<3f", body, o + 32)
        hmin, hmax, w, h = struct.unpack_from("<HHBB", body, o + 44)
        if (w, h) != (TILE_CELLS, TILE_CELLS):
            raise ValueError(f"tile ({tx},{ty},{tl}): unexpected size {w}x{h}")
        base = o + 52
        n = w * h
        arr = lambda k: np.frombuffer(body[base + k * n: base + (k + 1) * n], dtype=np.uint8).reshape(h, w)
        tiles.append(Tile(tx, ty, tl, bmin, bmax, arr(0), arr(1), arr(2)))
    return tiles


@dataclass
class Grid:
    """Region-local walkable grid. walk[row=z][col=x]; cell (col, row) covers x in [x0 + col*CELL, +CELL)."""
    x0: float
    z0: float
    walk: np.ndarray       # bool
    height: np.ndarray     # float32, NaN where unwalkable
    layers: int            # how many cells had more than one layer (bridges)

    @property
    def shape(self):
        return self.walk.shape

    def cell_of(self, x: float, z: float) -> tuple[int, int]:
        return int((x - self.x0) / CELL), int((z - self.z0) / CELL)

    def center(self, col: int, row: int) -> tuple[float, float]:
        return self.x0 + (col + 0.5) * CELL, self.z0 + (row + 0.5) * CELL


def walk_grid(tiles: list[Tile]) -> Grid:
    """Union of all layers per (x, z): a bridge and the passage under it become one walkable cell. Rare
    (counted in Grid.layers); acceptable for segmentation, revisit if a region relies on it."""
    if not tiles:
        raise ValueError("no tiles")
    tx0 = min(t.tx for t in tiles)
    ty0 = min(t.ty for t in tiles)
    w = (max(t.tx for t in tiles) - tx0 + 1) * TILE_CELLS
    h = (max(t.ty for t in tiles) - ty0 + 1) * TILE_CELLS
    walk = np.zeros((h, w), dtype=bool)
    height = np.full((h, w), np.nan, dtype=np.float32)
    count = np.zeros((h, w), dtype=np.uint8)
    for t in tiles:
        r, c = (t.ty - ty0) * TILE_CELLS, (t.tx - tx0) * TILE_CELLS
        sub = t.areas != 0
        hv = t.bmin[1] + t.heights.astype(np.float32) * CH
        blk = (slice(r, r + TILE_CELLS), slice(c, c + TILE_CELLS))
        walk[blk] |= sub
        cur = height[blk]
        height[blk] = np.where(sub & np.isnan(cur), hv, cur)
        count[blk] += sub
    return Grid(tx0 * TILE_UNITS, ty0 * TILE_UNITS, walk, height, int((count > 1).sum()))


@dataclass
class TerrainLayer:
    record: str                    # records/terraintextures/manmade/flat_fieldstonebrown.dbr
    mask: np.ndarray | None        # (128, 128) u8 opacity at 1 unit/sample, None for the base layer


def parse_terrain_layers(body: bytes) -> list[TerrainLayer]:
    """Note: the gaps between consecutive layer names are 16443..16449 bytes = 4 (next length prefix) + 16384
    (mask) + a few bytes of per-layer fields, except the first (base) layer which is followed directly by the
    next name. The few extra bytes are not decoded yet; the mask is taken as the 16384 bytes right after
    the name, which rendered correctly on 0A001."""
    refs = [(m.start(), m.group().decode()) for m in re.finditer(rb"records/terraintextures/[\w/]+\.dbr", body)]
    layers = []
    for i, (o, name) in enumerate(refs):
        start = o + len(name)
        nxt = refs[i + 1][0] if i + 1 < len(refs) else len(body)
        if nxt - start < TERRAIN_SAMPLES * TERRAIN_SAMPLES:
            layers.append(TerrainLayer(name, None))
        else:
            n = TERRAIN_SAMPLES * TERRAIN_SAMPLES
            layers.append(TerrainLayer(name, np.frombuffer(body[start: start + n], dtype=np.uint8).reshape(
                TERRAIN_SAMPLES, TERRAIN_SAMPLES)))
    return layers


def mask_to_cells(mask: np.ndarray, grid: Grid, origin_x: float = 0.0, origin_z: float = 0.0,
                  threshold: int = 128) -> np.ndarray:
    """Upsample a terrain mask (1 unit/sample) onto the grid's cells. The mask covers region-local
    [0, 128) x [0, 128), i.e. world [origin, origin + 128) where origin = the chunk's offset from world
    (tile coordinates are world coordinates; verified live 2026-08-22)."""
    k = int(TERRAIN_SAMPLE / CELL)
    up = np.kron(mask, np.ones((k, k), dtype=np.uint8)) > threshold
    out = np.zeros(grid.shape, dtype=bool)
    c0, r0 = grid.cell_of(origin_x, origin_z)
    h, w = grid.shape
    rs, cs = slice(max(r0, 0), min(r0 + up.shape[0], h)), slice(max(c0, 0), min(c0 + up.shape[1], w))
    if rs.stop > rs.start and cs.stop > cs.start:
        out[rs, cs] = up[rs.start - r0: rs.stop - r0, cs.start - c0: cs.stop - c0]
    return out
