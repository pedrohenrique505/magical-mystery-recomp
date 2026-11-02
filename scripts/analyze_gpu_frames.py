#!/usr/bin/env python3
"""Summarise a per-frame host GPU log written by --gpu_frame_csv (project/src/gpu_frame_log.cpp).

Usage: scripts/analyze_gpu_frames.py build/gpu_frames.csv [--from S] [--to S] [--budget MS]
                                     [--top N]

Prints:
  - the distribution of busy_ms (the frame's own GPU work) and interval_ms (GPU frame pace);
  - how many frames exceed the budget (default 16.7 ms) and in which seconds they cluster;
  - mean counters of over-budget frames vs the rest, sorted by ratio, which shows what the
    heavy frames have more of (texture loads, resolves, transfers, draws...);
  - the N heaviest frames with the two frames before each, to see what a spike looks like.
Times (--from/--to) are cpu_close_s, seconds since the first row.
"""

import argparse
import csv
import statistics
import sys
from collections import Counter

COUNTERS = [
    "submissions", "guest_draws", "draws", "render_passes", "barriers", "dispatches",
    "uploads", "upload_kb", "upload_rp_ends", "tex_loads", "tex_dispatches", "tex_kb",
    "resolves", "edram_dumps", "rt_transfer_draws", "clears_and_image_copies",
]
CATEGORIES = ["dump_ms", "resolve_ms", "texload_ms", "transfer_ms", "upload_ms"]


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    return s[min(len(s) - 1, int(p / 100 * len(s)))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--from", dest="t0", type=float, default=None)
    ap.add_argument("--to", dest="t1", type=float, default=None)
    ap.add_argument("--budget", type=float, default=16.7)
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args()

    with open(a.csv, newline="") as f:
        rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
    rows = [r for r in rows
            if (a.t0 is None or r["cpu_close_s"] >= a.t0) and (a.t1 is None or r["cpu_close_s"] <= a.t1)]
    if len(rows) < 3:
        sys.exit("not enough rows")

    busy = [r["busy_ms"] for r in rows]
    interval = [r["interval_ms"] for r in rows[1:]]
    span = rows[-1]["cpu_close_s"] - rows[0]["cpu_close_s"]
    print(f"{len(rows)} frames over {span:.1f} s ({len(rows) / max(span, 1e-9):.1f} fps)")
    for name, vals in (("busy_ms", busy), ("interval_ms", interval),
                       ("gap_ms", [r["gap_ms"] for r in rows[1:]])):
        print(f"  {name:12s} mean {statistics.fmean(vals):6.2f}  p50 {pct(vals, 50):6.2f}  "
              f"p90 {pct(vals, 90):6.2f}  p99 {pct(vals, 99):6.2f}  max {max(vals):6.2f}")

    if any(r.get("dump_ms", 0) for r in rows):
        print("\nGPU ms per frame by kind of work (--gpu_frame_categories):")
        for c in CATEGORIES + ["other_ms"]:
            vals = [r[c] if c != "other_ms" else r["busy_ms"] - sum(r[k] for k in CATEGORIES)
                    for r in rows]
            print(f"  {c:12s} mean {statistics.fmean(vals):6.2f}  p50 {pct(vals, 50):6.2f}  "
                  f"p90 {pct(vals, 90):6.2f}  max {max(vals):6.2f}")

    heavy = [r for r in rows if r["busy_ms"] > a.budget]
    late = [r for r in rows[1:] if r["interval_ms"] > a.budget * 1.5]
    print(f"\nbusy > {a.budget} ms: {len(heavy)} frames ({100 * len(heavy) / len(rows):.1f}%); "
          f"interval > {a.budget * 1.5:.1f} ms: {len(late)} frames")
    per_second = Counter(int(r["cpu_close_s"]) for r in heavy)
    if per_second:
        worst = sorted(per_second.items(), key=lambda kv: -kv[1])[:12]
        print("  seconds with most over-budget frames: " +
              ", ".join(f"{s}s:{n}" for s, n in sorted(worst)))

    if heavy and len(heavy) < len(rows):
        rest = [r for r in rows if r["busy_ms"] <= a.budget]
        print(f"\nmean per frame, over budget ({len(heavy)}) vs rest ({len(rest)}):")
        lines = []
        for c in ["busy_ms"] + COUNTERS + [k for k in CATEGORIES if k in rows[0]]:
            h = statistics.fmean(r[c] for r in heavy)
            n = statistics.fmean(r[c] for r in rest)
            lines.append((h / n if n else float("inf") if h else 1.0, c, h, n))
        for ratio, c, h, n in sorted(lines, key=lambda x: -x[0]):
            print(f"  {c:24s} {h:10.1f} {n:10.1f}   x{ratio:.2f}")

    print(f"\n{a.top} heaviest frames (with the two before):")
    cols = ["cpu_close_s", "busy_ms", "gpu_ms", "gap_ms", "interval_ms", "draws", "render_passes",
            "dispatches", "tex_loads", "tex_kb", "resolves", "edram_dumps", "rt_transfer_draws",
            "uploads", "upload_kb"]
    print("  " + " ".join(f"{c[:11]:>11s}" for c in cols))
    index = {id(r): i for i, r in enumerate(rows)}
    shown = set()
    for r in sorted(rows, key=lambda r: -r["busy_ms"])[:a.top]:
        i = index[id(r)]
        for j in range(max(0, i - 2), i + 1):
            if j in shown:
                continue
            shown.add(j)
            mark = "*" if j == i else " "
            print(mark + " " + " ".join(f"{rows[j][c]:11.2f}" for c in cols))
        print()


if __name__ == "__main__":
    main()
