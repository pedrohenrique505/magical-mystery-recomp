#!/usr/bin/env python3
"""Records a real gamepad's menu navigation as a macro for scripts/bench_song.sh.

Starts the game (scripts/run.sh, extra arguments pass through), and once the first frame is
logged ("world_rate: postproc", the same anchor bench_song.sh uses) records every button,
d-pad and stick change of your controller with its timing. Navigate as fast as you like to
the song you want to benchmark; recording stops by itself when gameplay starts
("emulate_fps=30.00" in the log), then the game is closed and the macro is written in
scripts/virtual_pad.py's command language (default build/bench_macro.txt). bench_song.sh
replays it at the same times when that file exists.

    .venv/bin/python -I scripts/record_pad.py [--out build/bench_macro.txt] [game args...]

Needs read access to the controller's /dev/input/event* node (the uaccess ACL is enough) and
python-evdev in the venv. Avoid pressing anything before the game window shows its first frame.
"""

import argparse
import os
import select
import signal
import subprocess
import sys
import time

import evdev
from evdev import ecodes as e

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
NAMES = {e.BTN_A: "A", e.BTN_B: "B", e.BTN_X: "X", e.BTN_Y: "Y", e.BTN_TL: "LB",
         e.BTN_TR: "RB", e.BTN_SELECT: "BACK", e.BTN_START: "START", e.BTN_MODE: "GUIDE",
         e.BTN_THUMBL: "LS", e.BTN_THUMBR: "RS"}
AXES = {e.ABS_X: "lx", e.ABS_Y: "ly", e.ABS_RX: "rx", e.ABS_RY: "ry", e.ABS_Z: "lt",
        e.ABS_RZ: "rt"}
ANCHOR = "world_rate: postproc"
GAMEPLAY = "emulate_fps=30.00"


def find_pad():
    for path in evdev.list_devices():
        try:
            d = evdev.InputDevice(path)
        except OSError:
            continue
        keys = d.capabilities().get(e.EV_KEY, [])
        # Skip our own virtual pad (uinput devices have no physical path).
        if e.BTN_A in keys and e.BTN_START in keys and d.phys:
            return d
    sys.exit("record_pad: no gamepad found under /dev/input (is it connected?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bench_macro.txt"))
    ap.add_argument("--timeout", type=float, default=300, help="give up after this many s")
    a, game_args = ap.parse_known_args()
    pad = find_pad()
    print(f"record_pad: recording {pad.name} ({pad.path})", flush=True)
    log = os.path.join(ROOT, "build", "bench_record.log")
    open(log, "w").close()
    env = dict(os.environ, RUN_LOG=log, REX_BIND_DEBUG_OVERLAY="none")
    game = subprocess.Popen([os.path.join(ROOT, "scripts", "run.sh"), *game_args], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    lines = []
    try:
        start = time.monotonic()
        anchor = None
        last = None
        abs_last = {}
        pos = 0
        while time.monotonic() - start < a.timeout:
            with open(log, errors="replace") as f:
                f.seek(pos)
                chunk = f.read()
                pos = f.tell()
            if anchor is None and ANCHOR in chunk:
                anchor = last = time.monotonic()
                print("record_pad: first frame seen, recording; go to your song", flush=True)
            if anchor is not None and GAMEPLAY in chunk:
                print("record_pad: gameplay started, stopping", flush=True)
                break
            if game.poll() is not None:
                sys.exit("record_pad: the game exited before gameplay started")
            r, _, _ = select.select([pad.fd], [], [], 0.05)
            if not r:
                continue
            for ev in pad.read():
                if anchor is None:
                    continue
                if ev.type == e.EV_KEY and ev.code in NAMES and ev.value in (0, 1):
                    cmd = f"key {NAMES[ev.code]} {ev.value}"
                elif ev.type == e.EV_ABS and ev.code in (e.ABS_HAT0X, e.ABS_HAT0Y):
                    cmd = f"hat {'x' if ev.code == e.ABS_HAT0X else 'y'} {ev.value}"
                elif ev.type == e.EV_ABS and ev.code in AXES:
                    # Sticks and triggers: keep only meaningful changes.
                    prev = abs_last.get(ev.code, 0)
                    big = 4000 if ev.code in STICK_CODES else 32
                    if abs(ev.value - prev) < big and not (ev.value == 0 and prev != 0):
                        continue
                    abs_last[ev.code] = ev.value
                    cmd = f"abs {AXES[ev.code]} {ev.value}"
                else:
                    continue
                now = time.monotonic()
                lines.append(f"wait {1000 * (now - last):.0f}")
                lines.append(cmd)
                last = now
        else:
            sys.exit("record_pad: timed out before gameplay started")
    finally:
        try:
            os.killpg(game.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        subprocess.run(["pkill", "-x", "beatles"], check=False)
    with open(a.out, "w") as f:
        f.write("# Recorded by scripts/record_pad.py; times are from the first logged frame.\n")
        f.write("\n".join(lines) + "\n")
    presses = sum(1 for l in lines if l.endswith(" 1") and l.startswith("key"))
    print(f"record_pad: {presses} button presses, {(last - anchor):.1f} s -> {a.out}")


STICK_CODES = (e.ABS_X, e.ABS_Y, e.ABS_RX, e.ABS_RY)

if __name__ == "__main__":
    main()
