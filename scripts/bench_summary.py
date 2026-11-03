#!/usr/bin/env python3
"""Summarise scripts/bench_song.sh runs: guest fps and host GPU time from gameplay start.

    python3 -I scripts/bench_summary.py <label> [<label>...] [--seconds N]

For each label reads build/bench_<label>.log (--perf_log windows of 2 s after the first
"emulate_fps=30.00" line) and build/bench_<label>.csv (per-frame GPU busy ms over the same
span, matched by wall time). Prints one row per label.
"""

import argparse
import csv
import datetime as dt
import os
import re
import statistics as st

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TS = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]")


def ts(line):
    m = TS.match(line)
    return dt.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f") if m else None


def summarise(label, seconds):
    log = os.path.join(ROOT, "build", f"bench_{label}.log")
    lines = open(log, errors="replace").read().splitlines()
    start = first = None
    fps = []
    full = []
    for line in lines:
        t = ts(line)
        if t and first is None and "perf:" in line:
            first = t
        if start is None:
            if "emulate_fps=30.00" in line:
                start = t
            continue
        if t and (t - start).total_seconds() > seconds:
            break
        m = re.search(r"perf: ([0-9.]+) fps", line)
        if m and (t - start).total_seconds() > 4:
            fps.append(float(m.group(1)))
        m = re.search(r"adaptive full rate (\d+)%", line)
        if m:
            full.append(int(m.group(1)))
    if start is None or not fps:
        return f"{label:24s} no gameplay found"
    out = (f"{label:24s} fps mean {st.mean(fps):5.1f} min {min(fps):5.1f} "
           f"<55: {100 * sum(f < 55 for f in fps) / len(fps):4.0f}% "
           f"<59: {100 * sum(f < 59 for f in fps) / len(fps):4.0f}%")
    path = os.path.join(ROOT, "build", f"bench_{label}.csv")
    if os.path.exists(path) and first is not None:
        # cpu_close_s is relative to the first CSV row, which is close to the first swap;
        # align on the first perf line (2 s after the first swap) minus 2 s.
        origin = first - dt.timedelta(seconds=2)
        lo = (start - origin).total_seconds() + 4
        hi = (start - origin).total_seconds() + seconds
        busy = sorted(float(r["busy_ms"]) for r in csv.DictReader(open(path))
                      if lo <= float(r["cpu_close_s"]) <= hi)
        if busy:
            q = lambda p: busy[int(p * (len(busy) - 1))]
            out += (f" | gpu ms mean {st.mean(busy):5.2f} p50 {q(.5):5.2f} p90 {q(.9):5.2f} "
                    f"p99 {q(.99):5.2f} >14: {100 * sum(b > 14 for b in busy) / len(busy):4.1f}%")
    if full:
        out += f" | full rate {st.mean(full):.0f}%"
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("labels", nargs="+")
    ap.add_argument("--seconds", type=float, default=60)
    a = ap.parse_args()
    for label in a.labels:
        print(summarise(label, a.seconds))


if __name__ == "__main__":
    main()
