"""Low-level keyboard hook monitor.

Installs a WH_KEYBOARD_LL hook on its own thread, injects a harmless probe key (VK_NONAME)
once a second via SendInput, and logs:
  - every real key event with the latency between the event timestamp and our hook
    being called (time spent in hooks ahead of us in the chain),
  - a loud line the moment a probe is NOT seen within 500 ms (== our hook was removed
    by the system, which is what happens when a hook in the chain times out).
Usage: uv run tools/hookmon.py [seconds]   (log: %LOCALAPPDATA%\\gdaccess\\hookmon.log)
"""
import ctypes, ctypes.wintypes as wt, os, sys, threading, time

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
WH_KEYBOARD_LL = 13
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
VK_NONAME = 0xFC
LLKHF_INJECTED = 0x10

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", wt.DWORD), ("scanCode", wt.DWORD), ("flags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]
class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]

HOOKPROC = ctypes.CFUNCTYPE(ctypes.c_ssize_t, ctypes.c_int, wt.WPARAM, wt.LPARAM)
user32.SetWindowsHookExW.restype = wt.HHOOK
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wt.HINSTANCE, wt.DWORD]
user32.CallNextHookEx.restype = ctypes.c_ssize_t
user32.CallNextHookEx.argtypes = [wt.HHOOK, ctypes.c_int, wt.WPARAM, wt.LPARAM]
kernel32.GetModuleHandleW.restype = ctypes.c_void_p
kernel32.GetModuleHandleW.argtypes = [wt.LPCWSTR]
user32.GetForegroundWindow.restype = ctypes.c_void_p
user32.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]

logdir = os.path.join(os.environ.get("LOCALAPPDATA", "."), "gdaccess")
os.makedirs(logdir, exist_ok=True)
logf = open(os.path.join(logdir, "hookmon.log"), "w", buffering=1, encoding="utf-8")
def log(s):
    line = f"{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d} {s}"
    logf.write(line + "\n"); print(line, flush=True)

probe_seen = threading.Event()
stats = {"events": 0, "max_lat": 0, "probes_missed": 0}
hhook = None

@HOOKPROC
def proc(code, wparam, lparam):
    if code == 0:
        k = ctypes.cast(lparam, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
        lat = (kernel32.GetTickCount() - k.time) & 0xFFFFFFFF
        if k.vkCode == VK_NONAME:
            probe_seen.set()
        else:
            stats["events"] += 1
            stats["max_lat"] = max(stats["max_lat"], lat)
            kind = "down" if wparam == WM_KEYDOWN else "up" if wparam == WM_KEYUP else hex(wparam)
            inj = " injected" if k.flags & LLKHF_INJECTED else ""
            fg = user32.GetForegroundWindow(); title = ctypes.create_unicode_buffer(64); user32.GetWindowTextW(fg, title, 64)
            log(f"key vk=0x{k.vkCode:02x} {kind}{inj} latency={lat}ms fg='{title.value}'")
    return user32.CallNextHookEx(hhook, code, wparam, lparam)

def hook_thread():
    global hhook
    hhook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, proc, kernel32.GetModuleHandleW(None), 0)
    log(f"hook installed: {hhook:#x}" if hhook else f"hook FAILED: {kernel32.GetLastError()}")
    msg = wt.MSG()
    while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
        user32.TranslateMessage(ctypes.byref(msg)); user32.DispatchMessageW(ctypes.byref(msg))

def send_probe():
    inp = (INPUT * 2)()
    for i, flags in enumerate((0, 2)):  # down, up (KEYEVENTF_KEYUP=2)
        inp[i].type = 1; inp[i].ki.wVk = VK_NONAME; inp[i].ki.dwFlags = flags; inp[i].ki.dwExtraInfo = 0x6D6F6E
    return user32.SendInput(2, inp, ctypes.sizeof(INPUT))

threading.Thread(target=hook_thread, daemon=True).start()
time.sleep(0.5)
duration = float(sys.argv[1]) if len(sys.argv) > 1 else 120
end = time.time() + duration
dead_since = None
while time.time() < end:
    probe_seen.clear()
    t0 = time.time(); send_probe()
    ok = probe_seen.wait(0.5)
    lat = int((time.time() - t0) * 1000)
    if ok:
        if dead_since: log(f"hook ALIVE again after {time.time()-dead_since:.1f}s"); dead_since = None
        if lat > 100: log(f"probe slow: {lat}ms")
    else:
        stats["probes_missed"] += 1
        if not dead_since: dead_since = time.time(); log("!!! probe NOT seen: our LL hook appears to have been REMOVED by the system")
    time.sleep(1.0)
log(f"done: {stats}")
