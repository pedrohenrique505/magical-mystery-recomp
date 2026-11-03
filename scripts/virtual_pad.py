#!/usr/bin/env python3
"""Virtual Xbox 360 gamepad (uinput) driven by text commands, for unattended test runs.

Creates a uinput device that SDL recognises as an Xbox 360 controller, then reads commands,
one per line, from a FIFO (default: $XDG_RUNTIME_DIR/beatles_pad) or from a script file:

    press A [ms]        tap a button (A B X Y LB RB BACK START GUIDE LS RS), default 120 ms
    hold A / release A  keep a button down / let it go
    dpad up|down|left|right [ms]
    stick lx|ly|rx|ry <value -32768..32767>
    trigger lt|rt <0..255>
    key A 1|0           raw button state (what scripts/record_pad.py writes)
    hat x|y -1|0|1      raw d-pad state
    abs lx|ly|rx|ry|lt|rt <value>   raw axis value
    wait <ms>
    quit

Usage:
    .venv/bin/python -I scripts/virtual_pad.py [--fifo PATH] [--script FILE]
Needs write access to /dev/uinput (a uaccess ACL is enough). Start it before the game so
SDL sees the controller at startup. Needs python-evdev in the venv.
"""

import argparse
import os
import sys
import time

from evdev import AbsInfo, UInput, ecodes as e

BUTTONS = {
    "A": e.BTN_A, "B": e.BTN_B, "X": e.BTN_X, "Y": e.BTN_Y,
    "LB": e.BTN_TL, "RB": e.BTN_TR, "BACK": e.BTN_SELECT, "START": e.BTN_START,
    "GUIDE": e.BTN_MODE, "LS": e.BTN_THUMBL, "RS": e.BTN_THUMBR,
}
STICKS = {"lx": e.ABS_X, "ly": e.ABS_Y, "rx": e.ABS_RX, "ry": e.ABS_RY}
TRIGGERS = {"lt": e.ABS_Z, "rt": e.ABS_RZ}
DPAD = {"up": (e.ABS_HAT0Y, -1), "down": (e.ABS_HAT0Y, 1),
        "left": (e.ABS_HAT0X, -1), "right": (e.ABS_HAT0X, 1)}


def make_device():
    stick = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
    trig = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
    hat = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
    caps = {
        e.EV_KEY: list(BUTTONS.values()),
        e.EV_ABS: [(c, stick) for c in STICKS.values()] + [(c, trig) for c in TRIGGERS.values()]
        + [(e.ABS_HAT0X, hat), (e.ABS_HAT0Y, hat)],
    }
    return UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=0x028E,
                  version=0x110, bustype=e.BUS_USB)


def run_command(ui, line):
    parts = line.split()
    if not parts or parts[0].startswith("#"):
        return True
    cmd, args = parts[0].lower(), parts[1:]
    if cmd == "quit":
        return False
    if cmd == "wait":
        time.sleep(float(args[0]) / 1000.0)
    elif cmd in ("press", "hold", "release"):
        code = BUTTONS[args[0].upper()]
        if cmd in ("press", "hold"):
            ui.write(e.EV_KEY, code, 1)
            ui.syn()
        if cmd == "press":
            time.sleep((int(args[1]) if len(args) > 1 else 120) / 1000.0)
        if cmd in ("press", "release"):
            ui.write(e.EV_KEY, code, 0)
            ui.syn()
    elif cmd == "dpad":
        code, value = DPAD[args[0].lower()]
        ui.write(e.EV_ABS, code, value)
        ui.syn()
        time.sleep((int(args[1]) if len(args) > 1 else 120) / 1000.0)
        ui.write(e.EV_ABS, code, 0)
        ui.syn()
    elif cmd == "key":
        ui.write(e.EV_KEY, BUTTONS[args[0].upper()], int(args[1]))
        ui.syn()
    elif cmd == "hat":
        ui.write(e.EV_ABS, e.ABS_HAT0X if args[0].lower() == "x" else e.ABS_HAT0Y, int(args[1]))
        ui.syn()
    elif cmd == "abs":
        name = args[0].lower()
        ui.write(e.EV_ABS, STICKS.get(name) or TRIGGERS[name], int(args[1]))
        ui.syn()
    elif cmd == "stick":
        ui.write(e.EV_ABS, STICKS[args[0].lower()], int(args[1]))
        ui.syn()
    elif cmd == "trigger":
        ui.write(e.EV_ABS, TRIGGERS[args[0].lower()], int(args[1]))
        ui.syn()
    else:
        print(f"virtual_pad: unknown command: {line}", file=sys.stderr)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    ap.add_argument("--fifo", default=os.path.join(runtime, "beatles_pad"))
    ap.add_argument("--script", help="run the commands in this file, then keep the pad until killed")
    a = ap.parse_args()
    ui = make_device()
    print(f"virtual_pad: created {ui.device.path if ui.device else 'device'}", flush=True)
    try:
        if a.script:
            with open(a.script) as f:
                for line in f:
                    if not run_command(ui, line.strip()):
                        return
            while True:
                time.sleep(3600)
        if not os.path.exists(a.fifo):
            os.mkfifo(a.fifo)
        print(f"virtual_pad: reading commands from {a.fifo}", flush=True)
        while True:
            with open(a.fifo) as f:
                for line in f:
                    if not run_command(ui, line.strip()):
                        return
    finally:
        ui.close()


if __name__ == "__main__":
    main()
