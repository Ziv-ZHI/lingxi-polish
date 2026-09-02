"""Capture real screenshots of the built BURNISH Qt applications.

Reproduces the assets under ``docs/assets/screenshots/`` so the README stays
grounded in the actual built binaries rather than mock-ups.

Usage (Windows; the host build was done with Qt 5.15.2 MinGW 8.1 64-bit)::

    # Build the host suite into build/<release|debug>/bin first
    qmake LingxiPolish.pro && mingw32-make -j4

    # Then capture
    pip install pillow pywin32
    python tools/screenshots/capture.py
"""
import argparse
import ctypes
import os
import subprocess
import time

import win32con
import win32gui
import win32process
from PIL import ImageGrab

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_BIN = os.path.join(REPO_ROOT, "build", "release", "bin")
DEFAULT_OUT = os.path.join(REPO_ROOT, "docs", "assets", "screenshots")

WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_RETURN = 0x0D
VK_SHIFT = 0x10


def _send_post(user32, hwnd, msg, wp, lp=0):
    inp = ctypes.create_string_buffer(28)  # INPUT on x64
    user32.PostMessageW(hwnd, msg, wp, lp)


def type_chars(user32, hwnd, s):
    """One WM_CHAR per character; delivered to the focused QLineEdit."""
    for ch in s:
        user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 0)
        time.sleep(0.03)


def press_enter(user32, hwnd):
    scan = user32.MapVirtualKeyW(VK_RETURN, 0) & 0xFF
    lpd = 1 | (scan << 16)
    lpu = lpd | (1 << 30) | (1 << 31)
    user32.PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, lpd)
    user32.PostMessageW(hwnd, WM_KEYUP, VK_RETURN, lpu)


def force_foreground(user32, hwnd):
    try:
        target_tid, _ = win32process.GetWindowThreadProcessId(hwnd)
    except Exception:
        target_tid = 0
    src_tid = ctypes.windll.kernel32.GetCurrentThreadId()
    attached = False
    if target_tid and target_tid != src_tid:
        try:
            user32.AttachThreadInput(src_tid, target_tid, True)
            attached = True
        except Exception:
            pass
    try:
        win32gui.ShowWindow(hwnd, win32con.SW_SHOWNORMAL)
        try:
            win32gui.SetForegroundWindow(hwnd)
        except Exception:
            pass
    finally:
        if attached:
            try:
                user32.AttachThreadInput(src_tid, target_tid, False)
            except Exception:
                pass
    time.sleep(0.3)


def keep_screen_awake(on=True):
    ES_CONTINUOUS = 0x80000000
    ES_SYSTEM_REQUIRED = 0x00000001
    ES_DISPLAY_REQUIRED = 0x00000002
    if on:
        ctypes.windll.kernel32.SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
        )
    else:
        ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS)


def list_windows(pid):
    out = []

    def cb(h, _):
        if not win32gui.IsWindowVisible(h):
            return
        try:
            _, wpid = win32process.GetWindowThreadProcessId(h)
        except Exception:
            return
        if wpid == pid:
            r = win32gui.GetWindowRect(h)
            out.append((h, win32gui.GetClassName(h), win32gui.GetWindowText(h), r))

    win32gui.EnumWindows(cb, None)
    return out


def wait_for_window(pid, *, title=None, max_area=None, min_area=200000,
                    exclude_titles=(), timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for h, cls, t, r in list_windows(pid):
            if title and t != title:
                continue
            if t in exclude_titles:
                continue
            area = (r[2] - r[0]) * (r[3] - r[1])
            if min_area and area < min_area:
                continue
            if max_area and area > max_area:
                continue
            return h
        time.sleep(0.4)
    return None


def find_main_window(pid, timeout=8.0):
    """Return the largest non-dialog window of the pid (the application shell)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        best = None
        for h, cls, t, r in list_windows(pid):
            if t in ("登录", "登录失败"):
                continue
            area = (r[2] - r[0]) * (r[3] - r[1])
            if area < 500000:
                continue
            if best is None or area > best[1]:
                best = (h, area)
        if best:
            return best[0]
        time.sleep(0.4)
    return None


def fill_screen(user32, hwnd):
    """Resize the window to the primary monitor's working area."""
    class RECT(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                    ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
    wa = RECT()
    ctypes.windll.user32.SystemParametersInfoW(0x0030, 0, ctypes.byref(wa), 0)
    w = wa.right - wa.left
    h = wa.bottom - wa.top
    user32.SetWindowPos(hwnd, 0, wa.left, wa.top, w, h, 0x0040)
    time.sleep(1.0)


def capture(hwnd, name, out_dir):
    rect = win32gui.GetWindowRect(hwnd)
    time.sleep(0.6)
    img = ImageGrab.grab(bbox=rect, all_screens=True)
    path = os.path.join(out_dir, name + ".png")
    img.save(path)
    return path, img.width, img.height


def shot_simple(user32, exe, name, bin_dir, out_dir, wait=3.0):
    proc = subprocess.Popen([os.path.join(bin_dir, exe)], cwd=bin_dir)
    hwnd = wait_for_window(proc.pid, min_area=200000)
    if hwnd is None:
        proc.kill()
        return f"NO_WINDOW {name}"
    time.sleep(wait)
    try:
        win32gui.ShowWindow(hwnd, win32con.SW_MAXIMIZE)
    except Exception:
        pass
    time.sleep(0.6)
    force_foreground(user32, hwnd)
    path, w, h = capture(hwnd, name, out_dir)
    proc.kill()
    time.sleep(0.6)
    return f"OK {name} {os.path.basename(path)} {w}x{h}"


def shot_login(user32, exe, name, bin_dir, out_dir,
               user="admin", pwd="lingxi@2026", wait_main=3.5):
    proc = subprocess.Popen([os.path.join(bin_dir, exe)], cwd=bin_dir)
    # Two successive QInputDialogs (name, then password).
    dlg1 = wait_for_window(proc.pid, title="登录", max_area=300000, timeout=10.0)
    if not dlg1:
        proc.kill()
        return f"NO_DIALOG1 {name}"
    force_foreground(user32, dlg1)
    time.sleep(0.4)
    type_chars(user32, dlg1, user)
    time.sleep(0.3)
    press_enter(user32, dlg1)
    time.sleep(1.6)

    dlg2 = None
    for h, cls, t, r in list_windows(proc.pid):
        if t == "登录" and h != dlg1 and 5000 < (r[2]-r[0])*(r[3]-r[1]) < 300000:
            dlg2 = h
            break
    if not dlg2:
        proc.kill()
        return f"NO_DIALOG2 {name}"
    force_foreground(user32, dlg2)
    time.sleep(0.4)
    type_chars(user32, dlg2, pwd)
    time.sleep(0.3)
    press_enter(user32, dlg2)
    time.sleep(wait_main)

    main = find_main_window(proc.pid)
    if not main:
        proc.kill()
        return f"LOGIN_FAILED {name}"
    try:
        win32gui.ShowWindow(main, win32con.SW_MAXIMIZE)
    except Exception:
        pass
    time.sleep(0.6)
    fill_screen(user32, main)
    force_foreground(user32, main)
    path, w, h = capture(main, name, out_dir)
    proc.kill()
    time.sleep(0.8)
    return f"OK {name} {os.path.basename(path)} {w}x{h}"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bin", default=DEFAULT_BIN, help="directory holding the built .exe files")
    p.add_argument("--out", default=DEFAULT_OUT, help="output directory for PNGs")
    args = p.parse_args()

    bin_dir = os.path.abspath(args.bin)
    out_dir = os.path.abspath(args.out)
    if not os.path.isdir(bin_dir):
        raise SystemExit(f"binary directory not found: {bin_dir}")
    os.makedirs(out_dir, exist_ok=True)

    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        ctypes.windll.user32.SetProcessDPIAware()
    keep_screen_awake(True)
    user32 = ctypes.windll.user32

    jobs = [
        ("VSCR-6EUR3-Monitor-Engineer.exe", "01-monitor-engineer", "login"),
        ("VSCR-6EUR3-Monitor-Operator.exe", "02-monitor-operator", "login"),
        ("DataAnalyzer.exe", "03-data-analyzer", "simple"),
        ("TrajectoryPlanner.exe", "04-trajectory-planner", "simple"),
        ("ProcessDB.exe", "05-process-db", "simple"),
    ]
    for exe, name, kind in jobs:
        if kind == "login":
            print(shot_login(user32, exe, name, bin_dir, out_dir))
        else:
            print(shot_simple(user32, exe, name, bin_dir, out_dir))

    # Remove debug artefacts (if any previous debug run left files behind).
    for f in os.listdir(out_dir):
        if f.startswith("_dbg_"):
            try:
                os.remove(os.path.join(out_dir, f))
            except OSError:
                pass
    keep_screen_awake(False)


if __name__ == "__main__":
    main()
