#!/usr/bin/env python3
"""Tap keys in an X window through XTest (no xdotool needed): focus the window, press and release each key.

Usage: xkey.py WINDOW_ID KEY [KEY ...]      KEY is an X keysym name (z, Return, Up, ...), a chord (Shift_L+F1), or "wait:SECONDS"
Used by tools/nogba.sh to start the cartridge from the DSi menu.
"""
import argparse
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

class ClientMessage(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong),
                ('send_event', ctypes.c_int), ('display', ctypes.c_void_p),
                ('window', ctypes.c_ulong), ('message_type', ctypes.c_ulong),
                ('format', ctypes.c_int), ('data', ctypes.c_long * 5)]

class XEvent(ctypes.Union):
    _fields_ = [('client', ClientMessage), ('padding', ctypes.c_long * 24)]

x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XInternAtom.restype = ctypes.c_ulong
x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                           ctypes.c_long, ctypes.POINTER(XEvent)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hold', type=float, default=0.15, help='seconds to hold each key (default: 0.15)')
    parser.add_argument('window', type=lambda value: int(value, 0))
    parser.add_argument('keys', nargs='+')
    args = parser.parse_args()
    if not 0 < args.hold <= 10:
        parser.error('--hold must be greater than zero and at most 10 seconds')
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        sys.exit("can't open the X display")
    win = args.window
    # Ask the window manager to activate the application too. Raw X focus
    # alone can leave Qt shortcuts inactive after another emulator closes.
    event = XEvent()
    event.client.type = 33  # ClientMessage
    event.client.display = dpy
    event.client.window = win
    event.client.message_type = x11.XInternAtom(dpy, b'_NET_ACTIVE_WINDOW', 0)
    event.client.format = 32
    event.client.data[0] = 2  # pager/tool source
    x11.XSendEvent(dpy, x11.XDefaultRootWindow(dpy), 0,
                   (1 << 20) | (1 << 19), ctypes.byref(event))
    x11.XFlush(dpy)
    time.sleep(0.2)
    x11.XRaiseWindow(dpy, win)
    x11.XSetInputFocus(dpy, win, 1, 0)  # RevertToPointerRoot, CurrentTime
    x11.XFlush(dpy)
    time.sleep(0.3)
    for key in args.keys:
        if key.startswith("wait:"):
            time.sleep(float(key[5:]))
            continue
        codes = [x11.XKeysymToKeycode(dpy, x11.XStringToKeysym(part.encode())) for part in key.split('+')]
        if not all(codes):
            sys.exit(f"unknown key {key}")
        for code in codes:
            xtst.XTestFakeKeyEvent(dpy, code, 1, 0)
        x11.XFlush(dpy)
        time.sleep(args.hold)
        for code in reversed(codes):
            xtst.XTestFakeKeyEvent(dpy, code, 0, 0)
        x11.XFlush(dpy)
        time.sleep(0.15)
    x11.XCloseDisplay(dpy)


if __name__ == "__main__":
    main()
