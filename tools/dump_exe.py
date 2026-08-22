"""Dump the running game's main module (SteamStub-unpacked in memory) to a file for offline analysis.
Usage: uv run --with pefile tools/dump_exe.py [out.bin]
Writes <out>.bin (raw image as mapped) and prints the base address; tools that analyze it should treat
file offset == RVA (the image is dumped by virtual layout, not file layout).
"""
import ctypes, ctypes.wintypes as wt, os, subprocess, sys

k32 = ctypes.WinDLL("kernel32", use_last_error=True); psapi = ctypes.WinDLL("psapi")
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wt.DWORD), ("EntryPoint", ctypes.c_void_p)]

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build", "GrimDawn.unpacked.bin")
pid = int(subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-Process 'Grim Dawn').Id"]).decode().strip())
proc = k32.OpenProcess(0x1F0FFF, False, pid)
hm = (ctypes.c_void_p * 2048)(); needed = wt.DWORD()
psapi.EnumProcessModulesEx(proc, hm, ctypes.sizeof(hm), ctypes.byref(needed), 3)
base = None
for i in range(needed.value // 8):
    buf = ctypes.create_unicode_buffer(260); psapi.GetModuleFileNameExW(proc, ctypes.c_void_p(hm[i]), buf, 260)
    if buf.value.lower().endswith("grim dawn.exe"):
        mi = MODULEINFO(); psapi.GetModuleInformation(proc, ctypes.c_void_p(hm[i]), ctypes.byref(mi), ctypes.sizeof(mi))
        base, size = hm[i], mi.SizeOfImage
if base is None: sys.exit("game module not found")
data = bytearray()
page = 0x10000
for off in range(0, size, page):
    n = min(page, size - off)
    b = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(proc, ctypes.c_void_p(base + off), b, n, ctypes.byref(got)): got.value = 0
    data += b.raw[:got.value] + b"\0" * (n - got.value)
with open(out, "wb") as f: f.write(data)
with open(out + ".base", "w") as f: f.write(f"{base:#x}\n")
print(f"dumped {len(data)} bytes, base {base:#x} -> {out}")
