"""world001.map: "MAP" v9. Header = quest file list, "Unique Entities" table, then the region table; the
regions' level bodies ("LVL" blobs, one per region) follow later in the same file at the offsets the table
gives. Level bodies are cached on disk (build/rooms/cache/<region>.lvl) because slicing the 819 MB map is slow.

Region record (measured 2026-08-22 on Region0A001/0A002): length-prefixed lvl path, u32 body offset, u32 body
size, 6 x i32 (world-grid placement), IntVec3 offset from
world, 16 bytes (GUID), length-prefixed world-map location record (records/ui/riftgatemap/locations/*.dbr)."""
from __future__ import annotations

import hashlib
import os
import re
import struct
from dataclasses import dataclass

from .arc import Arc

GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn"
LEVELS_ARC = os.path.join(GAME_DIR, "resources", "Levels.arc")
MAP_NAME = "world001.map"
HEAD_BYTES = 4 << 20          # the region table ends well inside the first 4 MB


@dataclass
class Region:
    name: str                 # "0A001"
    lvl_path: str
    offset: int
    size: int
    grid: tuple[int, ...]     # the 6 placement ints, raw (0x40, gx, 0x40, 0x40, gz, 0x40)
    world_offset: tuple[int, int, int]
    guid: bytes
    location: str             # location record basename, e.g. "riftgatemap1a_devilscrossing"
    index: int = 0            # record index = the live World::GetRegion(i) index
    shrine: str = ""          # devotion shrine record basename, if the chunk has one

    @property
    def underground(self) -> bool:
        return "Undergrounds" in self.lvl_path or self.name.startswith("0X")


class WorldMap:
    def __init__(self, arc_path: str = LEVELS_ARC, cache_dir: str = "build/rooms/cache"):
        self.arc = Arc(arc_path)
        self.cache_dir = cache_dir
        head = self.arc.slice(MAP_NAME, 0, HEAD_BYTES)
        self.regions: list[Region] = []
        self.quests: list[str] = [m.group().decode() for m in re.finditer(rb"Quests/[\w/]+\.qst", head)]
        # A record is: IntVec3 offset-from-world, 16-byte GUID, length-prefixed location record (may be
        # empty), length-prefixed lvl path, u32 body offset, u32 body size, 6 x i32 placement. The
        # IntVec3 that FOLLOWS a lvl path belongs to the next region (verified against the live
        # Region::GetOffsetFromWorld on 2026-08-22: 0A002 = (64, 0, -128)). Regions repeat lvl files
        # (633 records, 371 distinct files); identity is the record index = the live world index.
        for idx, m in enumerate(re.finditer(rb"Levels[/\\](?:[\w]+[/\\])*(\w+)\.lvl", head)):
            name = m.group(1).decode().removeprefix("Region")   # "0A001", "UG_CaveFlooded_A01", "AetherCity_A21"
            p = m.end()
            off, size = struct.unpack_from("<II", head, p)
            grid = struct.unpack_from("<6i", head, p + 8)
            # walk backwards: [len][lvl path] is preceded by [len][location] preceded by guid(16) preceded by IntVec3
            # ... [len][location record] [len][shrine record] u32 0 [len][lvl path] ...
            def string_before(end: int) -> tuple[int, str]:
                """A length-prefixed records/ string ending at `end` (possibly empty): (prefix pos, text)."""
                if struct.unpack_from("<I", head, end - 4)[0] == 0:
                    return end - 4, ""
                s = head.rfind(b"records/", max(0, end - 300), end)
                if s < 0 or struct.unpack_from("<I", head, s - 4)[0] != end - s:
                    raise ValueError(f"region record {idx} ({name}): cannot parse the string ending at {end:#x}")
                return s - 4, head[s:end].decode(errors="replace")
            q_shrine, shrine = string_before(m.start() - 4 - 4)
            q, loc = string_before(q_shrine)
            guid = head[q - 16: q]
            world_offset = struct.unpack_from("<3i", head, q - 28)
            loc = loc.rsplit("/", 1)[-1].removesuffix(".dbr")
            self.regions.append(Region(name, m.group().decode(), off, size, grid, world_offset, guid, loc, idx,
                                       shrine.rsplit("/", 1)[-1].removesuffix(".dbr")))
        self.by_name = {}
        for r in self.regions:          # first record wins for a repeated lvl file
            self.by_name.setdefault(r.name, r)

    def by_location(self, needle: str) -> list[Region]:
        return [r for r in self.regions if needle.lower() in r.location.lower()]

    def level_body(self, region: Region) -> bytes:
        os.makedirs(self.cache_dir, exist_ok=True)
        path = os.path.join(self.cache_dir, f"Region{region.name}_{region.size}.lvl")   # names collide (two 0W021s)
        if os.path.exists(path) and os.path.getsize(path) == region.size:
            with open(path, "rb") as f:
                return f.read()
        body = self.arc.slice(MAP_NAME, region.offset, region.size)
        with open(path, "wb") as f:
            f.write(body)
        return body

    @staticmethod
    def signature(body: bytes) -> str:
        """Content hash of a level body: the invalidation key for everything derived from it."""
        return hashlib.blake2b(body, digest_size=16).hexdigest()
