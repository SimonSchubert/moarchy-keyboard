#!/usr/bin/env python3
"""Synthesise touch events on the PinePhone, for verifying the keyboard.

Why this exists: this phone's seat has capabilities 6 -- keyboard and touch, no
pointer -- so `swaymsg seat - cursor set/press` is accepted and then emits
nothing, because there is no pointer device behind it. Verifying that a tap on a
key actually types therefore needs a real touch device, and this makes one
through /dev/uinput (protocol B multitouch, INPUT_PROP_DIRECT so libinput treats
it as a touchscreen rather than a touchpad).

Coordinates are PHYSICAL pixels (720x1440). The compositor runs at scale 2, so a
logical coordinate from the QML is twice as small: pass --scale 2 to give
logical ones instead.

Needs root for /dev/uinput. Run on the phone:

    sudo python3 tap.py 396,1140 180,1020        # two taps, physical
    sudo python3 tap.py --scale 2 198,570 90,510 # the same two, logical
"""

import argparse
import fcntl
import os
import struct
import sys
import time

UINPUT_IOCTL_BASE = ord("U")


def _iow(nr, size):
    return (1 << 30) | (size << 16) | (UINPUT_IOCTL_BASE << 8) | nr


def _io(nr):
    return (UINPUT_IOCTL_BASE << 8) | nr


UI_SET_EVBIT = _iow(100, 4)
UI_SET_KEYBIT = _iow(101, 4)
UI_SET_ABSBIT = _iow(103, 4)
UI_SET_PROPBIT = _iow(110, 4)
UI_DEV_CREATE = _io(1)
UI_DEV_DESTROY = _io(2)

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0x00
BTN_TOUCH = 0x14A
ABS_MT_SLOT = 0x2F
ABS_MT_POSITION_X = 0x35
ABS_MT_POSITION_Y = 0x36
ABS_MT_TRACKING_ID = 0x39
INPUT_PROP_DIRECT = 0x01

ABS_CNT = 64
UINPUT_MAX_NAME_SIZE = 80

EVENT = struct.Struct("llHHi")  # timeval (2x long) + type + code + value


class VirtualTouchscreen:
    def __init__(self, width, height, name=b"moarchy-test-touch"):
        self.width = width
        self.height = height
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)

        fcntl.ioctl(self.fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_KEYBIT, BTN_TOUCH)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
        for code in (ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID):
            fcntl.ioctl(self.fd, UI_SET_ABSBIT, code)

        absmin = [0] * ABS_CNT
        absmax = [0] * ABS_CNT
        absmax[ABS_MT_SLOT] = 9
        absmax[ABS_MT_POSITION_X] = width
        absmax[ABS_MT_POSITION_Y] = height
        absmin[ABS_MT_TRACKING_ID] = -1
        absmax[ABS_MT_TRACKING_ID] = 65535

        payload = name.ljust(UINPUT_MAX_NAME_SIZE, b"\0")
        payload += struct.pack("HHHH", 0x03, 0x1234, 0x5678, 1)  # bustype USB
        payload += struct.pack("I", 0)  # ff_effects_max
        payload += struct.pack(f"{ABS_CNT}i", *absmax)
        payload += struct.pack(f"{ABS_CNT}i", *absmin)
        payload += struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT))  # absfuzz
        payload += struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT))  # absflat
        os.write(self.fd, payload)

        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        # libinput has to notice the new device, and sway has to add it to the
        # seat AND map it to an output, before any event it sends lands where it
        # was aimed. Under-waiting here does not fail loudly: the first tap of a
        # run is simply swallowed, which reads as a flaky keyboard rather than a
        # flaky harness, and cost an hour of chasing the wrong bug.
        time.sleep(2.0)

    def _emit(self, type_, code, value):
        os.write(self.fd, EVENT.pack(0, 0, type_, code, value))

    def _sync(self):
        self._emit(EV_SYN, SYN_REPORT, 0)

    def tap(self, x, y, hold=0.09):
        self._emit(EV_ABS, ABS_MT_SLOT, 0)
        self._emit(EV_ABS, ABS_MT_TRACKING_ID, 1)
        self._emit(EV_ABS, ABS_MT_POSITION_X, int(x))
        self._emit(EV_ABS, ABS_MT_POSITION_Y, int(y))
        self._emit(EV_KEY, BTN_TOUCH, 1)
        self._sync()

        time.sleep(hold)

        self._emit(EV_ABS, ABS_MT_SLOT, 0)
        self._emit(EV_ABS, ABS_MT_TRACKING_ID, -1)
        self._emit(EV_KEY, BTN_TOUCH, 0)
        self._sync()

    def close(self):
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        finally:
            os.close(self.fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("points", nargs="+", metavar="X,Y")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="multiply each coordinate (use 2 to pass logical pixels)")
    parser.add_argument("--width", type=int, default=720)
    parser.add_argument("--height", type=int, default=1440)
    parser.add_argument("--hold", type=float, default=0.09,
                        help="seconds to hold each tap (raise past 0.4 for long-press)")
    parser.add_argument("--gap", type=float, default=0.22,
                        help="seconds between taps")
    parser.add_argument("--warmup", metavar="X,Y", default=None,
                        help="a throwaway tap before the real ones, to absorb "
                             "whatever the compositor still has to settle; give "
                             "somewhere harmless, e.g. inside the app area")
    args = parser.parse_args()

    if os.geteuid() != 0:
        sys.exit("tap.py needs root for /dev/uinput")

    device = VirtualTouchscreen(args.width, args.height)
    try:
        if args.warmup:
            wx, wy = (float(v) * args.scale for v in args.warmup.split(","))
            device.tap(wx, wy)
            print(f"warmup {wx:.0f},{wy:.0f}", flush=True)
            time.sleep(args.gap)

        for point in args.points:
            x, y = (float(v) * args.scale for v in point.split(","))
            device.tap(x, y, hold=args.hold)
            print(f"tapped {x:.0f},{y:.0f}", flush=True)
            time.sleep(args.gap)
    finally:
        device.close()


if __name__ == "__main__":
    main()
