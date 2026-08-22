"""List the top-level windows of a process (default: Grim Dawn) with class, title, visibility and the text of
their child controls -- for reading a modal dialog (MessageBox) the unfocused game put up.
Usage: uv run tools/windows.py [pid]"""
import ctypes, ctypes.wintypes as w, sys, subprocess

user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
EnumChildWindows = user32.EnumChildWindows
WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]


def text(h):
    n = user32.GetWindowTextLengthW(h)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(h, buf, n + 1)
    return buf.value


def cls(h):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, buf, 256)
    return buf.value


def pid_of(h):
    p = w.DWORD()
    GetWindowThreadProcessId(h, ctypes.byref(p))
    return p.value


def main():
    if len(sys.argv) > 1:
        pid = int(sys.argv[1])
    else:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Grim Dawn.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
        rows = [r for r in out.splitlines() if r.startswith('"')]
        if not rows:
            print("Grim Dawn is not running"); return
        pid = int(rows[0].split('","')[1])
    tops = []
    def cb(h, _):
        if pid_of(h) == pid: tops.append(h)
        return True
    EnumWindows(WNDENUMPROC(cb), 0)
    for h in tops:
        vis = user32.IsWindowVisible(h)
        print(f"hwnd={h:#x} class={cls(h)!r} visible={bool(vis)} title={text(h)!r}")
        kids = []
        def kcb(c, _):
            kids.append(c); return True
        EnumChildWindows(h, WNDENUMPROC(kcb), 0)
        for c in kids:
            t = text(c)
            if t: print(f"    child class={cls(c)!r} text={t!r}")


if __name__ == "__main__":
    main()
