"""Dev-loop client for gdaccess. Talks to the in-process dev server; never touches focus or real input.

  uv run --with pillow tools/gd.py launch [--speak]      build + launch unfocused + wait for /health
  uv run tools/gd.py health | text | buttons | log [--since N] | speech [--since N] [--wait SEC]
  uv run tools/gd.py say "text" | mute on|off | gamekeys on|off
  uv run tools/gd.py key enter | key a --ch a --shift | keys "hello"
  uv run tools/gd.py cursor X Y | cursor --clear
  uv run --with pillow tools/gd.py shot [out.png] [--width 1600]   screenshot of the game window (works unfocused)
  uv run tools/gd.py kill
"""
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import argparse, os, subprocess, sys, time, urllib.parse, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(os.environ.get("GDACCESS_PORT", "8791"))

def get(path, timeout=15):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}{path}", timeout=timeout) as r:
            return r.read().decode("utf-8", errors="replace")
    except OSError as e:  # the dev server is not up (no game, or the DLL is not loaded): one line, not a traceback
        sys.exit(f"dev server unreachable ({e}); run `gd.py status`")

def q(**kw):
    return "?" + urllib.parse.urlencode({k: v for k, v in kw.items() if v is not None and v is not False})

def cmd_launch(a):
    args = ["powershell", "-NoProfile", "-File", os.path.join(ROOT, "tools", "inject.ps1"), "-Launch"]
    if a.speak: args.append("-Speak")
    if a.nobuild: args.append("-NoBuild")
    sys.exit(subprocess.call(args))

def cmd_speech(a):
    since = a.since
    deadline = time.time() + (a.wait or 0)
    while True:
        out = get(f"/speech?since={since}")
        lines = out.splitlines()
        cur = int(lines[0].split(":")[1])
        body = lines[1:]
        if body or time.time() >= deadline:
            print(out, end="")
            return
        time.sleep(0.3)

def cmd_shot(a):
    import ctypes, ctypes.wintypes as wt
    from PIL import Image
    u32, g32 = ctypes.windll.user32, ctypes.windll.gdi32
    u32.FindWindowW.restype = ctypes.c_void_p
    hwnd = None
    # find the game's top-level window by process name
    import ctypes
    EnumWindows = u32.EnumWindows
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    pid_target = None
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Grim Dawn.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith('"Grim Dawn.exe"'):
            pid_target = int(line.split('","')[1]); break
    if not pid_target: sys.exit("game not running")
    found = []
    def cb(h, _):
        pid = wt.DWORD(); u32.GetWindowThreadProcessId(ctypes.c_void_p(h), ctypes.byref(pid))
        if pid.value == pid_target and u32.IsWindowVisible(ctypes.c_void_p(h)):
            buf = ctypes.create_unicode_buffer(256); u32.GetWindowTextW(ctypes.c_void_p(h), buf, 256)
            if buf.value: found.append(h)
        return True
    EnumWindows(WNDENUMPROC(cb), 0)
    if not found: sys.exit("no visible game window")
    hwnd = ctypes.c_void_p(found[0])
    r = wt.RECT(); u32.GetClientRect(hwnd, ctypes.byref(r)); w, h = r.right - r.left, r.bottom - r.top
    hdc = u32.GetDC(hwnd); mdc = g32.CreateCompatibleDC(hdc); bmp = g32.CreateCompatibleBitmap(hdc, w, h); g32.SelectObject(mdc, bmp)
    PW_CLIENTONLY, PW_RENDERFULLCONTENT = 1, 2
    if not u32.PrintWindow(hwnd, mdc, PW_CLIENTONLY | PW_RENDERFULLCONTENT): sys.exit("PrintWindow failed")
    class BMI(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]
    bi = BMI(); bi.biSize = ctypes.sizeof(BMI); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
    data = ctypes.create_string_buffer(w * h * 4)
    g32.GetDIBits(mdc, bmp, 0, h, data, ctypes.byref(bi), 0)
    img = Image.frombuffer("RGB", (w, h), data, "raw", "BGRX", 0, 1)
    g32.DeleteObject(bmp); g32.DeleteDC(mdc); u32.ReleaseDC(hwnd, hdc)
    if a.width and w > a.width: img = img.resize((a.width, int(h * a.width / w)))
    path = a.out or os.path.join(os.environ.get("LOCALAPPDATA", "."), "gdaccess", "shot.png")
    img.save(path); print(path)

def game_pids():
    """Pids of processes whose image is exactly Grim Dawn.exe (never anything else)."""
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Grim Dawn.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return [int(l.split('","')[1]) for l in out.splitlines() if l.startswith('"Grim Dawn.exe"')]

def cmd_kill(a):
    pids = game_pids()
    if not pids: print("Grim Dawn is not running"); return
    for pid in pids: subprocess.call(["taskkill", "/F", "/PID", str(pid)])

def cmd_status(a):
    """running / crashed / hung, decided from the health probe plus the process's dialogs and children.
    The game's crash handler keeps the process alive inside a dialog, so 'alive' alone proves nothing."""
    pids = game_pids()
    if not pids: print("status: not running"); return
    pid = pids[0]
    health = None
    try: health = get("/health", timeout=3).splitlines()[0]
    except Exception as e: health = None
    windows = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "windows.py"), str(pid)], capture_output=True, text=True).stdout
    dialogs = [l for l in windows.splitlines() if "class='#32770'" in l or "child class" in l]
    kids = subprocess.run(["powershell", "-NoProfile", "-Command", f"Get-CimInstance Win32_Process -Filter 'ParentProcessId={pid}' | Select-Object -ExpandProperty Name"],
                          capture_output=True, text=True).stdout.split()
    print(f"pid {pid}")
    if health: print("health:", health)
    for l in dialogs: print("dialog:", l.strip())
    for k in kids: print("child process:", k)
    if health and health.startswith("ok"): verdict = "running"
    elif dialogs or any("crash" in k.lower() for k in kids): verdict = "CRASHED (dialog up; the process only looks alive)"
    else: verdict = "HUNG or loading (no health, no dialog)"
    print("status:", verdict)

def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(f=cmd_status)
    s = sub.add_parser("launch"); s.add_argument("--speak", action="store_true"); s.add_argument("--nobuild", action="store_true"); s.set_defaults(f=cmd_launch)
    for name in ("health", "text", "buttons", "voices", "combat"):
        sub.add_parser(name).set_defaults(f=lambda a, n=name: print(get("/" + n), end=""))
    s = sub.add_parser("get"); s.add_argument("path"); s.set_defaults(f=lambda a: print(get(a.path), end=""))
    s = sub.add_parser("log"); s.add_argument("--since", type=int, default=0); s.set_defaults(f=lambda a: print(get(f"/log?since={a.since}"), end=""))
    s = sub.add_parser("speech"); s.add_argument("--since", type=int, default=0); s.add_argument("--wait", type=float, default=0); s.set_defaults(f=cmd_speech)
    s = sub.add_parser("say"); s.add_argument("text"); s.set_defaults(f=lambda a: print(get("/say" + q(text=a.text)), end=""))
    s = sub.add_parser("voice"); s.add_argument("text", nargs="?"); s.add_argument("--voice", default="mark"); s.add_argument("--pan", default="0"); s.add_argument("--since", type=int)
    s.set_defaults(f=lambda a: print(get("/voice" + (q(say=a.text, voice=a.voice, pan=a.pan) if a.text else q(since=a.since or 0))), end=""))
    s = sub.add_parser("mute"); s.add_argument("on"); s.set_defaults(f=lambda a: print(get("/mute" + q(on=a.on)), end=""))
    s = sub.add_parser("gamekeys"); s.add_argument("on"); s.set_defaults(f=lambda a: print(get("/gamekeys" + q(on=a.on)), end=""))
    s = sub.add_parser("key"); s.add_argument("name"); s.add_argument("--ch"); s.add_argument("--shift", action="store_true"); s.add_argument("--ctrl", action="store_true"); s.add_argument("--alt", action="store_true")
    s.set_defaults(f=lambda a: print(get("/key" + q(name=a.name if not a.name.startswith("0x") else None, code=a.name if a.name.startswith("0x") else None, ch=a.ch, shift=a.shift, ctrl=a.ctrl, alt=a.alt)), end=""))
    s = sub.add_parser("keys"); s.add_argument("text"); s.set_defaults(f=lambda a: print(get("/keys" + q(text=a.text)), end=""))
    s = sub.add_parser("cursor"); s.add_argument("x", nargs="?"); s.add_argument("y", nargs="?"); s.add_argument("--clear", action="store_true")
    s.set_defaults(f=lambda a: print(get("/cursor" + (q(clear=1) if a.clear else q(x=a.x, y=a.y))), end=""))
    s = sub.add_parser("click"); s.add_argument("x"); s.add_argument("y"); s.add_argument("--button", default="left"); s.set_defaults(f=lambda a: print(get("/click" + q(x=a.x, y=a.y, button=a.button)), end=""))
    s = sub.add_parser("mouse"); s.add_argument("type"); s.add_argument("x", nargs="?", default="0"); s.add_argument("y", nargs="?", default="0"); s.set_defaults(f=lambda a: print(get("/mouse" + q(type=a.type, x=a.x, y=a.y)), end=""))
    s = sub.add_parser("shot"); s.add_argument("out", nargs="?"); s.add_argument("--width", type=int, default=1600); s.set_defaults(f=cmd_shot)
    sub.add_parser("kill").set_defaults(f=cmd_kill)
    a = p.parse_args(); a.f(a)

if __name__ == "__main__":
    main()
