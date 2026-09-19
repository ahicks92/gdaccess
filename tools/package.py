"""Assemble the player zip from a finished build (CI runs this; it works locally too).

    python tools/package.py [--build build/ninja] [--out dist/gdaccess.zip] [--pdb-out dist/pdb]

Layout inside the zip (one top-level folder, so unzipping anywhere gives a self-contained mod folder):

    gdaccess/
      gdaccess.dll, prism.dll, gdinject.exe, launch.cmd (stopgap until the launcher exists)
      assets/          rooms.db, rooms_base.db, audio/...   -- the DLL loads these from next to itself
      README.md, LICENSE, THIRD_PARTY.md
      licenses/prism/  prism's NOTICE + LICENSES (MPL-2.0 attribution for the redistributed prism.dll)

The PDB is NOT in the zip; --pdb-out copies it beside it (the crash log records module+offset, so each released
build's gdaccess.pdb must be kept to symbolize a tester's log). No Python dependencies beyond the stdlib.
"""
import argparse, os, shutil, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRISM = os.path.join(ROOT, "third_party", "prism-bin", "prism-sdk-v0.18.1")

LAUNCH_CMD = r"""@echo off
rem Stopgap launcher (a proper one is coming): starts the 64-bit Steam game at its default path with the mod
rem injected before the game initializes. Steam must be running. Edit the path if the game lives elsewhere.
set "GAME=C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\x64\Grim Dawn.exe"
"%~dp0gdinject.exe" --launch "%GAME%" "%~dp0gdaccess.dll"
"""

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build", "ninja"))
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "gdaccess.zip"))
    ap.add_argument("--pdb-out", default=None, help="directory to copy gdaccess.pdb into (default: next to --out)")
    a = ap.parse_args()

    files = []   # (zip path, source path)
    def add(zpath, src):
        if not os.path.exists(src): sys.exit(f"package: missing {src}")
        files.append((zpath, src))
    def add_tree(zdir, srcdir):
        if not os.path.isdir(srcdir): sys.exit(f"package: missing directory {srcdir}")
        for dp, _, fns in os.walk(srcdir):
            for fn in sorted(fns):
                src = os.path.join(dp, fn)
                add(zdir + "/" + os.path.relpath(src, srcdir).replace(os.sep, "/"), src)

    for name in ("gdaccess.dll", "prism.dll", "gdinject.exe"):
        add("gdaccess/" + name, os.path.join(a.build, name))
    add_tree("gdaccess/assets", os.path.join(ROOT, "assets"))   # the repo copy, not the build's mirror of it
    add("gdaccess/README.md", os.path.join(ROOT, "README.md"))
    add("gdaccess/LICENSE", os.path.join(ROOT, "LICENSE"))
    add("gdaccess/THIRD_PARTY.md", os.path.join(ROOT, "third_party", "README.md"))
    add("gdaccess/licenses/prism/NOTICE", os.path.join(PRISM, "NOTICE"))
    add_tree("gdaccess/licenses/prism/LICENSES", os.path.join(PRISM, "LICENSES"))

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for zpath, src in files: z.write(src, zpath)
        z.writestr("gdaccess/launch.cmd", LAUNCH_CMD)
    pdb = os.path.join(a.build, "gdaccess.pdb")
    pdb_out = a.pdb_out or os.path.dirname(os.path.abspath(a.out))
    if os.path.exists(pdb):
        os.makedirs(pdb_out, exist_ok=True)
        shutil.copy2(pdb, os.path.join(pdb_out, "gdaccess.pdb"))
    else:
        print("package: no gdaccess.pdb next to the build (not fatal)")
    print(f"package: {a.out} ({os.path.getsize(a.out) / 1e6:.1f} MB, {len(files) + 1} files)")

if __name__ == "__main__":
    main()
