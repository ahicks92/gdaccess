"""Read Grim Dawn's database.arz offline (format 2 version 3: LZ4 records, shared string table) and print the
records whose path matches a regex, optionally only the fields whose name matches another regex.
Usage: uv run --with lz4 tools/arz.py <record-path-regex> [field-regex] [--max N]
  e.g. uv run --with lz4 tools/arz.py "records/skills/playerclass01/.*\\.dbr$" "range|radius|distance"
Format per reference/GDCommunityLauncher/extractor (ARZExtractor.cpp)."""
import re, struct, sys, lz4.block
P = r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\database\database.arz"

def load():
    d = open(P, "rb").read()
    fmt, ver, rec_start, rec_size, rec_count, str_start, str_size = struct.unpack_from("<HHIIIII", d, 0)
    assert (fmt, ver) == (2, 3), (fmt, ver)
    strings = []
    o = str_start
    n = struct.unpack_from("<I", d, o)[0]; o += 4
    for _ in range(n):
        ln = struct.unpack_from("<I", d, o)[0]; o += 4
        strings.append(d[o:o + ln].decode("utf-8", errors="replace")); o += ln
    recs = []
    o = rec_start
    for _ in range(rec_count):
        fid = struct.unpack_from("<I", d, o)[0]; o += 4
        ln = struct.unpack_from("<I", d, o)[0]; o += 4
        rname = d[o:o + ln].decode(errors="replace"); o += ln
        off, csz, dsz = struct.unpack_from("<III", d, o); o += 12
        o += 8  # data (timestamp)
        recs.append((strings[fid], rname, off, csz, dsz))
    return d, strings, recs

def decode(d, strings, off, csz, dsz):
    raw = lz4.block.decompress(d[off + 24:off + 24 + csz], uncompressed_size=dsz)
    i, out = 0, {}
    while i < len(raw):
        typ, cnt, key = struct.unpack_from("<HHI", raw, i); i += 8
        vals = []
        for _ in range(cnt):
            if typ == 1: vals.append(struct.unpack_from("<f", raw, i)[0])
            elif typ == 2: vals.append(strings[struct.unpack_from("<I", raw, i)[0]])
            elif typ == 3: vals.append(bool(struct.unpack_from("<i", raw, i)[0]))
            else: vals.append(struct.unpack_from("<i", raw, i)[0])
            i += 4
        out[strings[key]] = vals
    return out

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mx = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 50
    path_rx = re.compile(args[0], re.I)
    field_rx = re.compile(args[1], re.I) if len(args) > 1 else None
    d, strings, recs = load()
    shown = 0
    for path, rname, off, csz, dsz in recs:
        if not path_rx.search(path): continue
        rec = decode(d, strings, off, csz, dsz)
        lines = [f"  {k} = {v if len(v) > 1 else v[0]}" for k, v in sorted(rec.items()) if not field_rx or field_rx.search(k)]
        if field_rx and not lines: continue
        print(f"=== {path} ({rec.get('templateName', ['?'])[0]})")
        for l in lines: print(l)
        shown += 1
        if shown >= mx: print(f"... (--max {mx})"); break
