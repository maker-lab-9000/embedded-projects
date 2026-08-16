#!/usr/bin/env python3
"""Convert the device's soil.csv into a Markdown report.

Atlas Workspace (atlasworkspace.ai) imports Markdown notes, PDFs and
websites -- not raw CSV -- so this produces a Markdown note from the log.

Usage:
  python3 tools/export_report.py soil.csv [-o report.md]
      [--start 2026-08-16T21:00] [--threshold 30]

--start anchors minute 0 to a real timestamp. Time spent powered off is
not counted in `minute`, so timestamps after a reboot gap drift late.
"""

import argparse
import csv
import statistics
from datetime import datetime, timedelta

WATERING_JUMP = 5.0  # + points over 5 samples = watering event


def load(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                rows.append(
                    dict(boot=int(r["boot"]), minute=int(r["minute"]),
                         sensor=int(r["sensor"]), raw=int(r["raw"]),
                         pct=float(r["pct"]))
                )
            except (KeyError, ValueError, TypeError):
                continue  # skip malformed lines and #RESET marker rows
    return sorted(rows, key=lambda r: r["minute"])


def watering_events(rows):
    events = []
    for i in range(5, len(rows)):
        if rows[i]["pct"] - rows[i - 5]["pct"] > WATERING_JUMP:
            if not events or rows[i]["minute"] - events[-1] > 30:
                events.append(rows[i]["minute"])
    return events


def fmt_minute(minute, start):
    if start is None:
        return f"m{minute}"
    return (start + timedelta(minutes=minute)).strftime("%Y-%m-%d %H:%M")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("-o", "--out", default="report.md")
    ap.add_argument("--start", help="ISO timestamp of minute 0")
    ap.add_argument("--threshold", type=float, default=30.0)
    args = ap.parse_args()

    start = datetime.fromisoformat(args.start) if args.start else None
    rows = load(args.csv_path)
    if not rows:
        raise SystemExit("no valid samples in " + args.csv_path)

    pcts = [r["pct"] for r in rows]
    boots = len({r["boot"] for r in rows})
    events = watering_events(rows)
    last = rows[-1]

    # Downsample: hourly rows, or daily if the log spans more than ~8 days.
    span_min = rows[-1]["minute"] - rows[0]["minute"]
    bucket = 60 if span_min <= 8 * 1440 else 1440
    buckets = {}
    for r in rows:
        buckets.setdefault(r["minute"] // bucket, []).append(r["pct"])

    lines = [
        "# Soil moisture report — sensor #%d" % last["sensor"],
        "",
        "- Samples: %d (1/min), %d boot session(s)" % (len(rows), boots),
        "- Period: %s to %s" % (fmt_minute(rows[0]["minute"], start),
                                fmt_minute(last["minute"], start)),
        "- Moisture: last %.1f%%, mean %.1f%%, min %.1f%%, max %.1f%%"
        % (last["pct"], statistics.fmean(pcts), min(pcts), max(pcts)),
        "- Watering threshold: %.0f%%" % args.threshold,
        "- Watering events detected: %s"
        % (", ".join(fmt_minute(m, start) for m in events) or "none"),
        "",
        "| %s | mean %% | min %% | max %% |" % ("hour" if bucket == 60 else "day"),
        "|---|---|---|---|",
    ]
    for k in sorted(buckets):
        vals = buckets[k]
        lines.append("| %s | %.1f | %.1f | %.1f |"
                     % (fmt_minute(k * bucket, start),
                        statistics.fmean(vals), min(vals), max(vals)))
    lines += [
        "",
        "*`minute` does not advance while the device is powered off; "
        "timestamps after a reboot gap drift late.*",
    ]

    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d samples, %d table rows)" % (args.out, len(rows), len(buckets)))


if __name__ == "__main__":
    main()
