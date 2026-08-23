"""Room segmentation over a walkable grid. A port of wotr-access's RoomMap (persistence watershed over a
clearance field) reduced to 2-D, plus a walk-length cap: rooms whose geodesic diameter exceeds max_walk are
bisected at the halfway distance of their longest walk (the user's criterion: a room is bounded by the time
it takes to cross it). Roads are NOT an input; they are an overlay kept elsewhere.

Pipeline: furniture mask -> clearance (EDT) -> watershed (basins from clearance maxima, split where they meet
across a dip deeper than `persist`) -> BFS fill of sub-`cut_floor` slivers -> small-region merge -> walk cap
-> small-region merge -> stats, classes, exits (boundary runs between label pairs).

Cost (0A001, 69k walkable cells): the watershed is a Python loop (~15 us/cell); everything else is numpy /
scipy (geodesics = Dijkstra over a sparse 8-neighbour graph). Stage timings are in Segmentation.log."""
from __future__ import annotations

import time
from dataclasses import dataclass, field

import numpy as np
from scipy import ndimage, sparse
from scipy.sparse.csgraph import dijkstra

from .level import CELL, Grid


@dataclass
class Params:
    persist: float = 0.7        # clearance dip (units) needed to split two basins
    min_area: float = 12.0      # m^2; smaller regions merge into a neighbour
    cut_floor: float = 0.45     # cells with less clearance never seed a basin
    furniture_max: float = 12.0 # interior obstacle islands up to this cast no clearance shadow
    max_walk: float = 60.0      # geodesic diameter cap (units); 0 disables
    min_split_area: float = 40.0  # never bisect a room smaller than this
    island_min: float = 50.0    # walkable components not connected to the main one and under this are dropped


ALGO_VERSION = 1                # bump when the segmentation's output can change for the same input + params


@dataclass
class Room:
    id: int
    cells: int
    area: float
    centroid: tuple[float, float]
    anchor: tuple[float, float]      # the cell of maximum clearance: stable key for authored data
    mean_clear: float
    elongation: float
    cls: str
    walk: float                      # geodesic diameter estimate (units)
    bbox: tuple[float, float, float, float]   # x0, z0, x1, z1
    island: bool = False             # not connected to the region's main walkable component


@dataclass
class Exit:
    a: int
    b: int
    x: float
    z: float
    width: float
    cells: int
    cut: bool = False      # boundary made by the walk cap (no physical narrowing there)


@dataclass
class Segmentation:
    labels: np.ndarray           # int32 [row][col], -1 = unwalkable
    clearance: np.ndarray
    rooms: list[Room]
    exits: list[Exit]
    params: Params
    log: list[str] = field(default_factory=list)


N8 = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
S8 = np.ones((3, 3), dtype=bool)


# ----------------------------------------------------------------------------------------------- clearance

SEAM_BAND = 4      # cells; the bake leaves the first/last 4 cells of a tile un-eroded (measured: 0A001 x=64)


def seam_filter(walk: np.ndarray, tile: int = 128, band: int = SEAM_BAND) -> np.ndarray:
    """Drop walkable cells in the band along tile seams unless a walkable cell outside the band is within
    `band` cells of them. Grids start on a tile boundary, so seams are at multiples of `tile` cells."""
    h, w = walk.shape
    cols = np.arange(w) % tile
    rows = np.arange(h) % tile
    in_band = ((cols < band) | (cols >= tile - band))[None, :] | ((rows < band) | (rows >= tile - band))[:, None]
    interior = walk & ~in_band
    backed = ndimage.binary_dilation(interior, structure=np.ones((2 * band + 1, 2 * band + 1), dtype=bool))
    return walk & (~in_band | backed)


def furniture_fill(walk: np.ndarray, furniture_max: float) -> np.ndarray:
    """Unwalkable islands that do not touch the grid border and are at most furniture_max m^2."""
    lab, n = ndimage.label(~walk, structure=S8)
    if n == 0:
        return np.zeros_like(walk)
    sizes = np.bincount(lab.ravel())
    border = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    keep = np.zeros(n + 1, dtype=bool)
    keep[1:] = sizes[1:] * CELL * CELL <= furniture_max
    keep[border] = False
    keep[0] = False
    return keep[lab]


# ----------------------------------------------------------------------------------------------- watershed

def watershed(clear: np.ndarray, walk: np.ndarray, p: Params) -> np.ndarray:
    """Persistence watershed. Returns labels (int64, -1 for unlabelled); label values are flat cell indices."""
    h, w = clear.shape
    H, W = h + 2, w + 2
    c = np.zeros((H, W), dtype=np.float32)
    c[1:-1, 1:-1] = clear
    seed = np.zeros((H, W), dtype=bool)
    seed[1:-1, 1:-1] = walk & (clear >= p.cut_floor)
    cf = c.ravel().tolist()
    order = np.flatnonzero(seed.ravel())
    order = order[np.argsort(-c.ravel()[order], kind="stable")].tolist()
    parent = list(range(H * W))
    peak = list(cf)
    lab = [-1] * (H * W)
    offs = [dr * W + dc for dr, dc in N8]
    persist = p.persist

    def find(i):
        r = i
        while parent[r] != r:
            r = parent[r]
        while parent[i] != r:
            parent[i], i = r, parent[i]
        return r

    for idx in order:
        cv = cf[idx]
        roots = []
        for o in offs:
            l = lab[idx + o]
            if l >= 0:
                r = find(l)
                if r not in roots:
                    roots.append(r)
        if not roots:
            lab[idx] = idx
            peak[idx] = cv
            continue
        best = roots[0]
        for r in roots[1:]:
            if peak[r] > peak[best]:
                best = r
        lab[idx] = best
        for r in roots:
            if r != best and min(peak[r], peak[best]) - cv < persist:
                parent[r] = best
                if peak[r] > peak[best]:
                    peak[best] = peak[r]
    out = np.full(H * W, -1, dtype=np.int64)
    for idx in order:
        out[idx] = find(lab[idx])
    return out.reshape(H, W)[1:-1, 1:-1]


def fill_unlabelled(labels: np.ndarray, walk: np.ndarray) -> np.ndarray:
    """Walkable cells without a label take the label of an 8-neighbour, repeatedly."""
    lab = labels.copy()
    todo = walk & (lab < 0)
    while todo.any():
        grown = ndimage.maximum_filter(lab, size=3, mode="constant", cval=-1)
        newly = todo & (grown >= 0)
        if not newly.any():
            break   # sealed pockets with no labelled neighbour (clearance < cut_floor everywhere)
        lab[newly] = grown[newly]
        todo = walk & (lab < 0)
    return lab


def compact(labels: np.ndarray) -> np.ndarray:
    """Relabel to 0..n-1 (keeping -1)."""
    ids = np.unique(labels[labels >= 0])
    lut = np.full(int(labels.max()) + 2, -1, dtype=np.int32)
    lut[ids] = np.arange(len(ids), dtype=np.int32)
    return np.where(labels >= 0, lut[np.clip(labels, 0, None)], -1).astype(np.int32)


# ----------------------------------------------------------------------------------------------- borders

def border_pairs(labels: np.ndarray) -> np.ndarray:
    """(lo, hi) label pairs for every 4-neighbour edge between two different rooms (one row per edge)."""
    out = []
    for a, b in ((labels[:, :-1], labels[:, 1:]), (labels[:-1, :], labels[1:, :])):
        m = (a != b) & (a >= 0) & (b >= 0)
        out.append(np.stack([np.minimum(a[m], b[m]), np.maximum(a[m], b[m])], axis=1))
    return np.concatenate(out) if out else np.zeros((0, 2), dtype=np.int64)


def border_counts(labels: np.ndarray) -> dict[tuple[int, int], int]:
    pairs = border_pairs(labels)
    if len(pairs) == 0:
        return {}
    n = int(labels.max()) + 1
    key = pairs[:, 0].astype(np.int64) * n + pairs[:, 1]
    ks, cnt = np.unique(key, return_counts=True)
    return {(int(k // n), int(k % n)): int(c) for k, c in zip(ks, cnt)}


def merge_small(labels: np.ndarray, min_area: float, log: list[str]) -> np.ndarray:
    """Each pass merges every small region into its longest-border neighbour (resolving chains through
    a union-find so two small neighbours merging into each other still end up in one room)."""
    lab = compact(labels)
    min_cells = int(min_area / (CELL * CELL))
    merged = 0
    for _ in range(50):
        n = int(lab.max()) + 1
        if n <= 1:
            break
        sizes = np.bincount(lab[lab >= 0], minlength=n)
        small = np.flatnonzero((sizes > 0) & (sizes < min_cells))
        if len(small) == 0:
            break
        bc = border_counts(lab)
        best: dict[int, tuple[int, int]] = {}
        for (a, b), cnt in bc.items():
            for s, t in ((a, b), (b, a)):
                if sizes[s] < min_cells and (s not in best or cnt > best[s][0]):
                    best[s] = (cnt, t)
        if not best:
            lab[np.isin(lab, small)] = -1    # isolated tinies: drop
            break
        parent = np.arange(n)

        def find(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        for s, (_, t) in best.items():
            rs, rt = find(s), find(t)
            if rs != rt:
                # attach the small one under the other; if both small, either way
                parent[rs] = rt
        lut = np.array([find(i) for i in range(n)], dtype=np.int32)
        merged += int((lut != np.arange(n)).sum())
        lab = np.where(lab >= 0, lut[np.clip(lab, 0, None)], -1)
        lab = compact(lab)
    log.append(f"merged {merged} small regions")
    return lab


# ----------------------------------------------------------------------------------------------- geodesics

class CellGraph:
    """Sparse 8-neighbour graph over walkable cells with euclidean edge weights (cells), for Dijkstra."""

    def __init__(self, walk: np.ndarray):
        h, w = walk.shape
        self.shape = walk.shape
        self.index = np.full(h * w, -1, dtype=np.int64)
        flat = np.flatnonzero(walk.ravel())
        self.index[flat] = np.arange(len(flat))
        self.cells = flat
        rows, cols, wts = [], [], []
        idx2 = self.index.reshape(h, w)
        for dr, dc in ((0, 1), (1, 0), (1, 1), (1, -1)):
            a = idx2[max(0, -dr): h - max(0, dr), max(0, -dc): w - max(0, dc)]
            b = idx2[max(0, dr): h - max(0, -dr), max(0, dc): w - max(0, -dc)]
            m = (a >= 0) & (b >= 0)
            rows.append(a[m]); cols.append(b[m]); wts.append(np.full(int(m.sum()), np.hypot(dr, dc)))
        r = np.concatenate(rows); c = np.concatenate(cols); v = np.concatenate(wts)
        n = len(flat)
        self.g = sparse.csr_matrix((np.concatenate([v, v]), (np.concatenate([r, c]), np.concatenate([c, r]))), shape=(n, n))

    def sub(self, mask: np.ndarray):
        """Node ids of the walkable cells under mask, and the induced subgraph."""
        ids = self.index[np.flatnonzero(mask.ravel())]
        ids = ids[ids >= 0]
        return ids, self.g[ids][:, ids]


def diameter(cg: CellGraph, mask: np.ndarray) -> tuple[float, np.ndarray, np.ndarray]:
    """Double-sweep longest shortest path within mask: (length in units, node ids, distance from the far end)."""
    ids, g = cg.sub(mask)
    if len(ids) < 2:
        return 0.0, ids, np.zeros(len(ids))
    d0 = dijkstra(g, directed=False, indices=0)
    d0[~np.isfinite(d0)] = -1
    a = int(np.argmax(d0))
    d1 = dijkstra(g, directed=False, indices=a)
    d1[~np.isfinite(d1)] = -1
    return float(d1.max() * CELL), ids, d1


def walk_cap(labels: np.ndarray, cg: CellGraph, p: Params, log: list[str],
             cuts: np.ndarray | None = None) -> np.ndarray:
    """cuts (bool grid, optional) receives the cells on the near side of every bisection boundary."""
    if p.max_walk <= 0:
        return labels
    lab = labels.copy()
    splits = 0
    queue = list(range(int(lab.max()) + 1))
    min_cells = p.min_split_area / (CELL * CELL)
    min_side = int(8 / CELL)   # never cut a room whose longer side is under 8 units (spirals, etc.)
    while queue:
        r = queue.pop()
        mask = lab == r
        if mask.sum() < min_cells:
            continue
        length, ids, d = diameter(cg, mask)
        if length <= p.max_walk:
            continue
        # Axis-aligned cut (decided 2026-08-22: the announcements ARE the player's perception, so a
        # boundary must be simple to hold in the head, i.e. a north-south or east-west line). Cut across
        # the bounding box's longer axis, in its middle third, where the room's cross-section is narrowest.
        ys, xs = np.nonzero(mask)
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        if max(y1 - y0, x1 - x0) < min_side:
            continue
        sub = mask[y0:y1, x0:x1]
        if (x1 - x0) >= (y1 - y0):
            profile = sub.sum(axis=0)           # cells per column -> cut between columns
            lo, hi = len(profile) // 3, max(len(profile) // 3 + 1, 2 * len(profile) // 3)
            k = lo + int(np.argmin(profile[lo:hi]))
            piece_local = np.zeros_like(sub); piece_local[:, :k] = sub[:, :k]
        else:
            profile = sub.sum(axis=1)
            lo, hi = len(profile) // 3, max(len(profile) // 3 + 1, 2 * len(profile) // 3)
            k = lo + int(np.argmin(profile[lo:hi]))
            piece_local = np.zeros_like(sub); piece_local[:k, :] = sub[:k, :]
        piece = np.zeros_like(mask); piece[y0:y1, x0:x1] = piece_local
        rest = mask & ~piece
        if not piece.any() or not rest.any():
            continue
        if cuts is not None:
            cuts |= piece & ndimage.binary_dilation(rest, structure=S8)
            cuts |= rest & ndimage.binary_dilation(piece, structure=S8)
        # each side may fall apart into several components; every component becomes its own room
        new_ids = []
        for side in (piece, rest):
            comp, n = ndimage.label(side, structure=S8)
            for i in range(1, n + 1):
                new_id = int(lab.max()) + 1
                lab[comp == i] = new_id
                new_ids.append(new_id)
        splits += 1
        queue.extend(new_ids)
    log.append(f"walk cap: {splits} bisections")
    return compact(lab)


# ----------------------------------------------------------------------------------------------- output

def exits_of(labels: np.ndarray, grid: Grid, cuts: np.ndarray | None = None) -> list[Exit]:
    """Openings = connected runs of boundary cells between a given pair of rooms."""
    out = []
    h, w = labels.shape
    pairs = border_pairs(labels)
    if len(pairs) == 0:
        return out
    n = int(labels.max()) + 1
    # per-room bounding boxes to keep the per-pair work local
    objs = ndimage.find_objects(labels + 1)
    keys = np.unique(pairs[:, 0].astype(np.int64) * n + pairs[:, 1])
    for key in keys:
        a, b = int(key // n), int(key % n)
        sa, sb = objs[a], objs[b]
        if sa is None or sb is None:
            continue
        r0, r1 = max(0, min(sa[0].start, sb[0].start) - 1), min(h, max(sa[0].stop, sb[0].stop) + 1)
        c0, c1 = max(0, min(sa[1].start, sb[1].start) - 1), min(w, max(sa[1].stop, sb[1].stop) + 1)
        sub = labels[r0:r1, c0:c1]
        ma, mb = sub == a, sub == b
        da = ndimage.binary_dilation(ma, structure=ndimage.generate_binary_structure(2, 1))
        db = ndimage.binary_dilation(mb, structure=ndimage.generate_binary_structure(2, 1))
        touch = (ma & db) | (mb & da)
        comp, k = ndimage.label(touch, structure=S8)
        for i in range(1, k + 1):
            ys, xs = np.nonzero(comp == i)
            cx, cz = grid.center(float(xs.mean() + c0), float(ys.mean() + r0))
            width = max(xs.max() - xs.min(), ys.max() - ys.min()) * CELL + CELL
            is_cut = bool(cuts is not None and cuts[r0:r1, c0:c1][comp == i].mean() > 0.5)
            out.append(Exit(a, b, cx, cz, float(width), len(ys), is_cut))
    return out


def classify(area: float, elong: float, mean_clear: float) -> str:
    if elong > 2.6 and mean_clear < 2.2:
        return "passage"
    if elong > 3.2:
        return "corridor"
    if area < 35:
        return "small"
    if area > 220:
        return "hall"
    return "room"


def room_stats(labels: np.ndarray, clear: np.ndarray, grid: Grid, cg: CellGraph) -> list[Room]:
    rooms = []
    n = int(labels.max()) + 1
    objs = ndimage.find_objects(labels + 1)
    for r in range(n):
        sl = objs[r]
        if sl is None:
            continue
        sub = labels[sl] == r
        ys, xs = np.nonzero(sub)
        ys, xs = ys + sl[0].start, xs + sl[1].start
        cells = len(ys)
        area = cells * CELL * CELL
        cx, cz = grid.center(float(xs.mean()), float(ys.mean()))
        cv = clear[ys, xs]
        k = int(np.argmax(cv))
        ax, az = grid.center(int(xs[k]), int(ys[k]))
        if cells > 2:
            cov = np.cov(np.stack([xs, ys]).astype(np.float64))
            ev = np.sort(np.linalg.eigvalsh(cov))
            elong = float(np.sqrt(ev[1] / ev[0])) if ev[0] > 1e-6 else 99.0
        else:
            elong = 1.0
        length, _, _ = diameter(cg, labels == r)
        bx0, bz0 = grid.center(int(xs.min()), int(ys.min()))
        bx1, bz1 = grid.center(int(xs.max() + 1), int(ys.max() + 1))
        rooms.append(Room(r, cells, area, (cx, cz), (ax, az), float(cv.mean()), elong,
                          classify(area, elong, float(cv.mean())), length, (bx0, bz0, bx1, bz1)))
    return rooms


def segment(grid: Grid, p: Params | None = None) -> Segmentation:
    p = p or Params()
    log: list[str] = []
    t = time.time()

    def lap(msg):
        nonlocal t
        log.append(f"{msg} ({time.time() - t:.1f}s)")
        t = time.time()

    walk = seam_filter(grid.walk)
    # islands: components not connected to the largest one; tiny ones are ledges and decoration spots
    comp, ncomp = ndimage.label(walk, structure=S8)
    comp_sizes = np.bincount(comp.ravel())
    main_comp = int(np.argmax(comp_sizes[1:])) + 1 if ncomp else 0
    tiny = np.zeros(ncomp + 1, dtype=bool)
    tiny[1:] = comp_sizes[1:] * CELL * CELL < p.island_min
    tiny[main_comp] = False
    walk = walk & ~tiny[comp]
    log.append(f"islands: {ncomp - 1} components besides the main one, {int(tiny.sum())} dropped")
    fill = furniture_fill(walk, p.furniture_max)
    clear = ndimage.distance_transform_edt(walk | fill).astype(np.float32) * CELL
    clear[~walk] = 0.0
    lap(f"clearance: {int(fill.sum())} furniture cells shadow-free")
    lab = compact(watershed(clear, walk, p))
    lap(f"watershed: {int(lab.max()) + 1} basins")
    lab = fill_unlabelled(lab, walk)
    lab = merge_small(lab, p.min_area, log)
    lap(f"after merge: {int(lab.max()) + 1} rooms")
    cg = CellGraph(walk)
    cuts = np.zeros(walk.shape, dtype=bool)
    lab = walk_cap(lab, cg, p, log, cuts)
    lab = merge_small(lab, p.min_area, log)
    lap(f"after walk cap: {int(lab.max()) + 1} rooms")
    rooms = room_stats(lab, clear, grid, cg)
    # stable, readable ids: order rooms by their anchor (z, then x) and relabel accordingly
    order = sorted(range(len(rooms)), key=lambda i: (rooms[i].anchor[1], rooms[i].anchor[0]))
    lut = np.full(int(lab.max()) + 2, -1, dtype=np.int32)
    for new_id, i in enumerate(order):
        lut[rooms[i].id] = new_id
    lab = np.where(lab >= 0, lut[np.clip(lab, 0, None)], -1).astype(np.int32)
    rooms = [rooms[i] for i in order]
    for new_id, r in enumerate(rooms):
        r.id = new_id
        c, rr = grid.cell_of(*r.anchor)
        r.island = bool(comp[rr, c] != main_comp) if 0 <= rr < comp.shape[0] and 0 <= c < comp.shape[1] else False
    exits = exits_of(lab, grid, cuts)
    lap(f"{len(rooms)} rooms, {len(exits)} exits")
    return Segmentation(lab, clear, rooms, exits, p, log)


def stitch(parts: list[tuple[Grid, tuple[float, float]]]) -> Grid:
    """Combine region grids placed at offsets (x, z) into one grid."""
    xs0 = [g.x0 + ox for g, (ox, oz) in parts]
    zs0 = [g.z0 + oz for g, (ox, oz) in parts]
    xs1 = [g.x0 + ox + g.shape[1] * CELL for g, (ox, oz) in parts]
    zs1 = [g.z0 + oz + g.shape[0] * CELL for g, (ox, oz) in parts]
    X0, Z0 = min(xs0), min(zs0)
    W = int(round((max(xs1) - X0) / CELL))
    H = int(round((max(zs1) - Z0) / CELL))
    walk = np.zeros((H, W), dtype=bool)
    height = np.full((H, W), np.nan, dtype=np.float32)
    layers = 0
    for g, (ox, oz) in parts:
        c0 = int(round((g.x0 + ox - X0) / CELL))
        r0 = int(round((g.z0 + oz - Z0) / CELL))
        h, w = g.shape
        walk[r0:r0 + h, c0:c0 + w] |= g.walk
        blk = height[r0:r0 + h, c0:c0 + w]
        height[r0:r0 + h, c0:c0 + w] = np.where(np.isnan(blk), g.height, blk)
        layers += g.layers
    return Grid(X0, Z0, walk, height, layers)
