"""Disassemble code from the dumped (unpacked) exe image, annotating calls into Engine.dll/Game.dll by export name.
Usage: uv run --with pefile --with capstone tools/exe_dis.py <rva-hex> [count] [<rva-hex> [count] ...]
Needs build/GrimDawn.unpacked.bin (+ .base) from tools/dump_exe.py and a running game for module bases.
"""
import bisect, ctypes, ctypes.wintypes as wt, os, re, struct, subprocess, sys
import capstone, pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
G = r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\x64"
img = open(os.path.join(ROOT, "build", "GrimDawn.unpacked.bin"), "rb").read()
base = int(open(os.path.join(ROOT, "build", "GrimDawn.unpacked.bin.base")).read().strip(), 16)

# live module bases (for resolving IAT pointers to module+offset)
mods = {}
try:
    k32 = ctypes.WinDLL("kernel32"); psapi = ctypes.WinDLL("psapi"); k32.OpenProcess.restype = wt.HANDLE
    pid = int(subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-Process 'Grim Dawn').Id"]).decode().strip())
    proc = k32.OpenProcess(0x1F0FFF, False, pid)
    hm = (ctypes.c_void_p * 2048)(); needed = wt.DWORD()
    psapi.EnumProcessModulesEx(proc, hm, ctypes.sizeof(hm), ctypes.byref(needed), 3)
    for i in range(needed.value // 8):
        buf = ctypes.create_unicode_buffer(260); psapi.GetModuleFileNameExW(proc, ctypes.c_void_p(hm[i]), buf, 260)
        mods[os.path.basename(buf.value).lower()] = hm[i]
except Exception as e:
    print("(no live process; calls into DLLs will not be named)", e)

# export tables for Engine/Game (sorted RVAs)
exports = {}
dbg = ctypes.windll.dbghelp
def und(n):
    b = ctypes.create_string_buffer(1024)
    return b.value.decode() if dbg.UnDecorateSymbolName(n.encode(), b, 1024, 0x1000 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80 | 0x100) else n
for dll in ("Engine.dll", "Game.dll", "DirectInput.dll"):
    try:
        pe = pefile.PE(os.path.join(G, dll), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        exports[dll.lower()] = sorted((s.address, s.name.decode()) for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name)
    except Exception:
        pass

def modname(addr):
    best = None
    for name, b in mods.items():
        if b <= addr and (best is None or b > mods[best]): best = name
    if best is None: return f"{addr:#x}"
    off = addr - mods[best]
    if best in exports:
        rvas = [a for a, _ in exports[best]]
        i = bisect.bisect_right(rvas, off) - 1
        if i >= 0 and off - rvas[i] < 0x2000:
            return f"{best}!{und(exports[best][i][1])[:70]}+{off - rvas[i]:#x}"
    return f"{best}+{off:#x}"

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def dis(rva, count):
    print(f"=== exe+{rva:#x} ({modname(base + rva)}) ===")
    for i, ins in enumerate(md.disasm(img[rva:rva + count * 16], base + rva)):
        extra = ""
        m = re.search(r"\[rip ([+-]) (0x[0-9a-f]+)\]", ins.op_str)
        if m:
            t = ins.address + ins.size + (int(m.group(2), 16) if m.group(1) == "+" else -int(m.group(2), 16))
            trva = t - base
            if ins.mnemonic in ("call", "jmp") and 0 <= trva < len(img) - 8:
                extra = "   ; -> " + modname(struct.unpack_from("<Q", img, trva)[0])
            elif 0 <= trva < len(img):
                extra = f"   ; [exe+{trva:#x}]"
        elif ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            extra = f"   ; exe+{int(ins.op_str, 16) - base:#x}"
        print(f"  +{ins.address - base:#x}: {ins.mnemonic} {ins.op_str}{extra}")
        if i >= count: break

args = sys.argv[1:]
i = 0
while i < len(args):
    rva = int(args[i], 16); count = 60
    if i + 1 < len(args) and args[i + 1].isdigit(): count = int(args[i + 1]); i += 1
    dis(rva, count); i += 1
