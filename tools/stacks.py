"""Dump native stacks of all threads in a process (x64), resolving symbols from PDBs/exports via dbghelp.
Usage: uv run tools/stacks.py <pid|"Grim Dawn.exe"> [max_threads]
Threads are suspended one at a time just long enough to walk them.
"""
import ctypes, ctypes.wintypes as wt, sys, os

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp", use_last_error=True)

PROCESS_ALL_ACCESS = 0x1F0FFF
THREAD_ALL_ACCESS = 0x1F03FF
TH32CS_SNAPTHREAD = 4
TH32CS_SNAPPROCESS = 2
CONTEXT_FULL = 0x10000B
IMAGE_FILE_MACHINE_AMD64 = 0x8664

class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_ulonglong), ("High", ctypes.c_longlong)]
class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [
        ("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong), ("P3Home", ctypes.c_ulonglong),
        ("P4Home", ctypes.c_ulonglong), ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
        ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
        ("SegCs", wt.WORD), ("SegDs", wt.WORD), ("SegEs", wt.WORD), ("SegFs", wt.WORD), ("SegGs", wt.WORD), ("SegSs", wt.WORD),
        ("EFlags", wt.DWORD),
        ("Dr0", ctypes.c_ulonglong), ("Dr1", ctypes.c_ulonglong), ("Dr2", ctypes.c_ulonglong), ("Dr3", ctypes.c_ulonglong),
        ("Dr6", ctypes.c_ulonglong), ("Dr7", ctypes.c_ulonglong),
        ("Rax", ctypes.c_ulonglong), ("Rcx", ctypes.c_ulonglong), ("Rdx", ctypes.c_ulonglong), ("Rbx", ctypes.c_ulonglong),
        ("Rsp", ctypes.c_ulonglong), ("Rbp", ctypes.c_ulonglong), ("Rsi", ctypes.c_ulonglong), ("Rdi", ctypes.c_ulonglong),
        ("R8", ctypes.c_ulonglong), ("R9", ctypes.c_ulonglong), ("R10", ctypes.c_ulonglong), ("R11", ctypes.c_ulonglong),
        ("R12", ctypes.c_ulonglong), ("R13", ctypes.c_ulonglong), ("R14", ctypes.c_ulonglong), ("R15", ctypes.c_ulonglong),
        ("Rip", ctypes.c_ulonglong),
        ("FltSave", ctypes.c_byte * 512),
        ("VectorRegister", M128A * 26), ("VectorControl", ctypes.c_ulonglong),
        ("DebugControl", ctypes.c_ulonglong), ("LastBranchToRip", ctypes.c_ulonglong), ("LastBranchFromRip", ctypes.c_ulonglong),
        ("LastExceptionToRip", ctypes.c_ulonglong), ("LastExceptionFromRip", ctypes.c_ulonglong)]
class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_ulonglong), ("Segment", wt.WORD), ("Mode", ctypes.c_int)]
class KDHELP64(ctypes.Structure):
    _fields_ = [("Thread", ctypes.c_ulonglong), ("ThCallbackStack", wt.DWORD), ("ThCallbackBStore", wt.DWORD), ("NextCallback", wt.DWORD),
                ("FramePointer", wt.DWORD), ("KiCallUserMode", ctypes.c_ulonglong), ("KeUserCallbackDispatcher", ctypes.c_ulonglong),
                ("SystemRangeStart", ctypes.c_ulonglong), ("KiUserExceptionDispatcher", ctypes.c_ulonglong),
                ("StackBase", ctypes.c_ulonglong), ("StackLimit", ctypes.c_ulonglong),
                ("BuildVersion", wt.DWORD), ("RetpolineStubFunctionTableSize", wt.DWORD), ("RetpolineStubFunctionTable", ctypes.c_ulonglong),
                ("RetpolineStubOffset", wt.DWORD), ("RetpolineStubSize", wt.DWORD), ("Reserved0", ctypes.c_ulonglong * 2)]
class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64), ("AddrStack", ADDRESS64), ("AddrBStore", ADDRESS64),
                ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_ulonglong * 4), ("Far", wt.BOOL), ("Virtual", wt.BOOL),
                ("Reserved", ctypes.c_ulonglong * 3), ("KdHelp", KDHELP64)]
class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD), ("th32OwnerProcessID", wt.DWORD),
                ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long), ("dwFlags", wt.DWORD)]
class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wt.DWORD), ("szExeFile", ctypes.c_wchar * 260)]
class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("TypeIndex", wt.DWORD), ("Reserved", ctypes.c_ulonglong * 2), ("Index", wt.DWORD), ("Size", wt.DWORD),
                ("ModBase", ctypes.c_ulonglong), ("Flags", wt.DWORD), ("Value", ctypes.c_ulonglong), ("Address", ctypes.c_ulonglong),
                ("Register", wt.DWORD), ("Scope", wt.DWORD), ("Tag", wt.DWORD), ("NameLen", wt.DWORD), ("MaxNameLen", wt.DWORD), ("Name", ctypes.c_char * 1024)]

k32.OpenProcess.restype = wt.HANDLE; k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenThread.restype = wt.HANDLE; k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.SuspendThread.argtypes = [wt.HANDLE]; k32.ResumeThread.argtypes = [wt.HANDLE]
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.Thread32First.argtypes = [wt.HANDLE, ctypes.c_void_p]; k32.Thread32Next.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.c_void_p]; k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.c_void_p]
dbg.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
dbg.SymSetOptions.argtypes = [wt.DWORD]
dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(ctypes.c_ulonglong), ctypes.c_void_p]
dbg.SymGetModuleBase64.restype = ctypes.c_ulonglong; dbg.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_ulonglong]
dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p; dbg.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_ulonglong]
dbg.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
dbg.SymGetModuleInfoW64 = None
dbg.UnDecorateSymbolName.argtypes = [ctypes.c_char_p, ctypes.c_char_p, wt.DWORD, wt.DWORD]

def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32W(); pe.dwSize = ctypes.sizeof(pe)
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == name.lower(): return pe.th32ProcessID
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    return 0

def exe_name_of(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32W(); pe.dwSize = ctypes.sizeof(pe)
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.th32ProcessID == pid: return pe.szExeFile
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    return ""

arg = sys.argv[1] if len(sys.argv) > 1 else "Grim Dawn.exe"
pid = int(arg) if arg.isdigit() else find_pid(arg)
if not pid: sys.exit("Grim Dawn is not running")
# Never suspend the threads of anything but the game: a pid from a dead instance may have been reused.
if exe_name_of(pid).lower() != "grim dawn.exe": sys.exit(f"pid {pid} is '{exe_name_of(pid)}', not Grim Dawn.exe; refusing")
max_threads = int(sys.argv[2]) if len(sys.argv) > 2 else 12
proc = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
if not proc: sys.exit(f"OpenProcess({pid}) failed: {ctypes.get_last_error()}")
dbg.SymSetOptions(0x2 | 0x4 | 0x10)  # UNDNAME | DEFERRED_LOADS | LOAD_LINES
if not dbg.SymInitializeW(proc, None, True): sys.exit(f"SymInitialize failed: {ctypes.get_last_error()}")

mods = {}
import pefile  # noqa (only to name modules by base)
psapi = ctypes.WinDLL("psapi")
hm = (ctypes.c_void_p * 2048)(); needed = wt.DWORD()
psapi.EnumProcessModulesEx(proc, hm, ctypes.sizeof(hm), ctypes.byref(needed), 0x03)
for i in range(needed.value // 8):
    buf = ctypes.create_unicode_buffer(260)
    psapi.GetModuleFileNameExW(proc, ctypes.c_void_p(hm[i]), buf, 260)
    mods[hm[i]] = os.path.basename(buf.value)
def modname(addr):
    best = None
    for b in mods:
        if b <= addr and (best is None or b > best): best = b
    return (mods[best], addr - best) if best is not None and addr - best < 0x10000000 else ("?", addr)

def sym(addr):
    si = SYMBOL_INFO(); si.SizeOfStruct = 88; si.MaxNameLen = 1000
    disp = ctypes.c_ulonglong()
    m, off = modname(addr)
    if dbg.SymFromAddr(proc, addr, ctypes.byref(disp), ctypes.byref(si)):
        name = si.Name.decode(errors="replace")
        und = ctypes.create_string_buffer(1024)
        if name.startswith("?") and dbg.UnDecorateSymbolName(name.encode(), und, 1024, 0x1000 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80 | 0x100): name = und.value.decode(errors="replace")
        return f"{m}!{name}+0x{disp.value:x}"
    return f"{m}+0x{off:x}"

snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te)
threads = []
ok = k32.Thread32First(snap, ctypes.byref(te))
while ok:
    if te.th32OwnerProcessID == pid: threads.append(te.th32ThreadID)
    ok = k32.Thread32Next(snap, ctypes.byref(te))
print(f"pid {pid}: {len(threads)} threads; showing first {max_threads} plus any in gdaccess/user32/Engine frames")

def walk(tid):
    ht = k32.OpenThread(THREAD_ALL_ACCESS, False, tid)
    if not ht: return None
    k32.SuspendThread(ht)
    try:
        ctx = CONTEXT(); ctx.ContextFlags = CONTEXT_FULL
        if not k32.GetThreadContext(ht, ctypes.byref(ctx)): return None
        fr = STACKFRAME64()
        fr.AddrPC.Offset = ctx.Rip; fr.AddrPC.Mode = 3
        fr.AddrFrame.Offset = ctx.Rbp; fr.AddrFrame.Mode = 3
        fr.AddrStack.Offset = ctx.Rsp; fr.AddrStack.Mode = 3
        frames = []
        for _ in range(40):
            if not dbg.StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, ht, ctypes.byref(fr), ctypes.byref(ctx), None,
                                   dbg.SymFunctionTableAccess64, dbg.SymGetModuleBase64, None): break
            if fr.AddrPC.Offset == 0: break
            frames.append(sym(fr.AddrPC.Offset))
        return frames
    finally:
        k32.ResumeThread(ht)

shown = 0
for tid in threads:
    frames = walk(tid)
    if frames is None: continue
    interesting = any(("gdaccess" in f or "Engine.dll" in f or "Game.dll" in f or "user32" in f.lower() or "DirectInput" in f) for f in frames)
    if shown < max_threads or interesting:
        shown += 1
        print(f"\n--- thread {tid} ---")
        for f in frames: print("   ", f)
