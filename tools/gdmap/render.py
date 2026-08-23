"""Floor-plan PNGs of a segmentation: rooms in stable colours with their ids at the anchor, unwalkable black,
room borders dark, exits as white dots, optional road overlay. North (-z) is up: row 0 is the smallest z."""
from __future__ import annotations

import colorsys

import numpy as np
from PIL import Image, ImageDraw

from .level import CELL, Grid
from .rooms import Segmentation


def room_color(i: int) -> tuple[int, int, int]:
    h = (i * 0.618033988749895) % 1.0
    s = 0.55 + 0.25 * ((i * 7) % 3) / 2
    v = 0.75 + 0.2 * ((i * 5) % 2)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


def render(seg: Segmentation, grid: Grid, path: str, scale: int = 2, roads: np.ndarray | None = None,
           ids: bool = True) -> None:
    lab = seg.labels
    h, w = lab.shape
    img = np.zeros((h, w, 3), dtype=np.uint8)
    n = int(lab.max()) + 1
    pal = np.array([room_color(i) for i in range(max(n, 1))], dtype=np.uint8)
    m = lab >= 0
    img[m] = pal[lab[m]]
    # borders: a cell whose right or lower neighbour has another label
    border = np.zeros((h, w), dtype=bool)
    border[:, :-1] |= (lab[:, :-1] != lab[:, 1:]) & (lab[:, :-1] >= 0) & (lab[:, 1:] >= 0)
    border[:-1, :] |= (lab[:-1, :] != lab[1:, :]) & (lab[:-1, :] >= 0) & (lab[1:, :] >= 0)
    img[border] = (img[border] * 0.35).astype(np.uint8)
    if roads is not None:
        rm = roads & m
        img[rm] = (img[rm] * 0.5 + np.array([255, 255, 255]) * 0.5).astype(np.uint8)
    im = Image.fromarray(img).resize((w * scale, h * scale), Image.NEAREST)
    d = ImageDraw.Draw(im)
    for e in seg.exits:
        c, r = grid.cell_of(e.x, e.z)
        x, y = c * scale, r * scale
        rad = max(2, scale) + (2 if e.cut else 0)
        d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=(255, 40, 40) if e.cut else (255, 255, 255),
                  outline=(0, 0, 0))
    if ids:
        for room in seg.rooms:
            c, r = grid.cell_of(*room.anchor)
            x, y = c * scale, r * scale
            txt = str(room.id)
            tw = 6 * len(txt) + 2
            d.rectangle([x - tw // 2, y - 6, x + tw // 2, y + 6], fill=(0, 0, 0))
            d.text((x - tw // 2 + 1, y - 6), txt, fill=(255, 255, 255))
    im.save(path)


def render_grid(grid: Grid, path: str, scale: int = 1, clearance: np.ndarray | None = None) -> None:
    h, w = grid.shape
    img = np.zeros((h, w, 3), dtype=np.uint8)
    if clearance is not None:
        c = np.clip(clearance / 16.0, 0, 1)
        img[..., 0] = (grid.walk * 60 + c * 195).astype(np.uint8)
        img[..., 1] = (grid.walk * 60 + c * 120).astype(np.uint8)
        img[..., 2] = (grid.walk * 60).astype(np.uint8)
    else:
        img[grid.walk] = (90, 90, 90)
    im = Image.fromarray(img)
    if scale != 1:
        im = im.resize((w * scale, h * scale), Image.NEAREST)
    im.save(path)
