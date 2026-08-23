"""Grim Dawn .arc container (version 3): a part table (LZ4-block compressed chunks) and an entry table whose
entries own a contiguous run of parts. Entries can be read whole or sliced without decompressing the rest,
which matters for world001.map (819 MB uncompressed, 3124 parts)."""
from __future__ import annotations

import bisect
import struct
from dataclasses import dataclass

import lz4.block


@dataclass
class Entry:
    name: str
    size: int                 # uncompressed
    parts: list[tuple[int, int, int]]   # (offset in file, compressed size, uncompressed size)
    starts: list[int]         # cumulative uncompressed start of each part


class Arc:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        magic, ver, num_entries, num_parts, rec_size, str_size, rec_off = struct.unpack_from("<IIIIIII", d, 0)
        if magic != 0x435241 or ver != 3:   # "ARC\0"
            raise ValueError(f"{path}: not a v3 arc (magic {magic:#x} ver {ver})")
        parts = [struct.unpack_from("<III", d, rec_off + 12 * i) for i in range(num_parts)]
        strs = d[rec_off + rec_size: rec_off + rec_size + str_size]
        ent = rec_off + rec_size + str_size
        self.entries: dict[str, Entry] = {}
        for i in range(num_entries):
            (typ, off, csz, usz, adler, ft1, ft2, np_, pi, slen, so) = struct.unpack_from("<11I", d, ent + 44 * i)
            name = strs[so: strs.index(b"\0", so)].decode(errors="replace")
            ps = parts[pi: pi + np_]
            starts, acc = [], 0
            for _, _, u in ps:
                starts.append(acc)
                acc += u
            self.entries[name] = Entry(name, usz, ps, starts)

    def names(self) -> list[str]:
        return list(self.entries)

    def _part(self, p: tuple[int, int, int]) -> bytes:
        o, c, u = p
        chunk = self.data[o: o + c]
        return chunk if c == u else lz4.block.decompress(chunk, uncompressed_size=u)

    def read(self, name: str) -> bytes:
        e = self.entries[name]
        return b"".join(self._part(p) for p in e.parts)

    def slice(self, name: str, start: int, length: int) -> bytes:
        """Uncompressed bytes [start, start+length) of an entry, decompressing only the parts that overlap."""
        e = self.entries[name]
        if start < 0 or start + length > e.size:
            raise ValueError(f"{name}: slice {start}+{length} outside {e.size}")
        out = bytearray()
        j = bisect.bisect_right(e.starts, start) - 1
        while len(out) < length and j < len(e.parts):
            blob = self._part(e.parts[j])
            a = max(0, start - e.starts[j])
            out += blob[a: a + (length - len(out))]
            j += 1
        return bytes(out)
