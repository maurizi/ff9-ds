#!/usr/bin/env python3
"""Tap keys in an X window through XTest (no xdotool needed): focus the window, press and release each key.

Usage: xkey.py WINDOW_ID KEY [KEY ...]      KEY is an X keysym name (z, Return, Up, ...) or "wait:SECONDS"
Used by tools/nogba.sh to start the cartridge from the DSi menu.
"""
import ctypes
import sys
import time

x11 = ctypes.cdll.LoadLibrary("libX11.so.6")
xtst = ctypes.cdll.LoadLibrary("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]


def main():
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        sys.exit("can't open the X display")
    win = int(sys.argv[1], 0)
    x11.XRaiseWindow(dpy, win)
    x11.XSetInputFocus(dpy, win, 1, 0)  # RevertToPointerRoot, CurrentTime
    x11.XFlush(dpy)
    time.sleep(0.3)
    for key in sys.argv[2:]:
        if key.startswith("wait:"):
            time.sleep(float(key[5:]))
            continue
        code = x11.XKeysymToKeycode(dpy, x11.XStringToKeysym(key.encode()))
        if not code:
            sys.exit(f"unknown key {key}")
        xtst.XTestFakeKeyEvent(dpy, code, 1, 0)
        x11.XFlush(dpy)
        time.sleep(0.15)
        xtst.XTestFakeKeyEvent(dpy, code, 0, 0)
        x11.XFlush(dpy)
        time.sleep(0.15)
    x11.XCloseDisplay(dpy)


if __name__ == "__main__":
    main()
