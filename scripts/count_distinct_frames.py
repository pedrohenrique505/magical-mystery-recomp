#!/usr/bin/env python3
"""Counts distinct frames per second in a constant-frame-rate screen capture.

The guest swap counter (--perf_log) says how often the game presents, not how often
the picture changes. This decodes a capture (e.g. gpu-screen-recorder -fm cfr -f 60),
downscales it to grayscale, and calls a frame a repeat when its mean absolute
difference from the previous frame is below a threshold (encoder noise). Prints one
line per second (distinct frames, longest run of repeats) and a summary.

Usage: count_distinct_frames.py capture.mp4 [--threshold 0.6] [--start S] [--end S]
Needs ffmpeg and numpy. Slow for long captures: run it in the background.
"""
import argparse
import subprocess
import sys

import numpy as np

W, H = 320, 180


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--threshold", type=float, default=0.6,
                    help="mean abs diff (0-255) below which a frame is a repeat")
    ap.add_argument("--fps", type=float, default=60.0, help="capture frame rate")
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--end", type=float, default=0.0)
    args = ap.parse_args()

    cmd = ["ffmpeg", "-v", "error", "-ss", str(args.start)]
    if args.end > args.start:
        cmd += ["-to", str(args.end)]
    cmd += ["-i", args.capture, "-vf", f"scale={W}:{H},format=gray",
            "-f", "rawvideo", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    frame_bytes = W * H
    prev = None
    diffs = []
    while True:
        buf = proc.stdout.read(frame_bytes)
        if len(buf) < frame_bytes:
            break
        cur = np.frombuffer(buf, dtype=np.uint8).astype(np.int16)
        if prev is not None:
            diffs.append(float(np.abs(cur - prev).mean()))
        prev = cur
    proc.wait()
    if not diffs:
        print("no frames decoded", file=sys.stderr)
        return 1

    new = np.array(diffs) >= args.threshold
    per_sec = int(round(args.fps))
    totals = []
    for i in range(0, len(new) - per_sec + 1, per_sec):
        window = new[i:i + per_sec]
        run = longest = 0
        for is_new in window:
            run = 0 if is_new else run + 1
            longest = max(longest, run)
        totals.append(int(window.sum()))
        print(f"t={args.start + i / args.fps:7.1f}s distinct {int(window.sum()):3d}/{per_sec}"
              f"  longest repeat run {longest}")
    t = np.array(totals)
    print(f"summary: {len(t)} s, distinct/s mean {t.mean():.1f}, median {np.median(t):.0f}, "
          f"min {t.min()}, seconds below 55: {int((t < 55).sum())}, below 45: {int((t < 45).sum())}")
    print(f"diff quantiles (p10/p50/p90): {np.percentile(diffs, [10, 50, 90]).round(2).tolist()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
