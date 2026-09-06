#!/usr/bin/env python3
"""Magnetometer helpers for the orbital-density /obs.csv log.

  python3 tools/mag_calib.py offsets obs.csv [--start S --end S]
      Hard-iron offsets from a slow full rotation of the instrument: the midpoint of
      min/max on each axis. Paste the three numbers into MAG_OFF_X/Y/Z in mmc5603.h.

  python3 tools/mag_calib.py stability obs.csv [--start S --end S]
      Mean and standard deviation of |B| over a window: run once per arm length
      (15, 20, 25, 30 cm) while toggling the backlight and SD writes; pick the shortest
      arm whose std-dev stops improving.

--start/--end select rows by uptime_s so one file can hold several experiments.
"""
import argparse, csv, math, sys


def load(path, start, end):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                t = int(r["uptime_s"])
                x, y, z = float(r["mag_x_ut"]), float(r["mag_y_ut"]), float(r["mag_z_ut"])
            except (KeyError, ValueError):
                continue  # header mismatch or empty (sensor absent) fields
            if start is not None and t < start:
                continue
            if end is not None and t > end:
                continue
            rows.append((t, x, y, z))
    if not rows:
        sys.exit("no usable mag rows in the selected window")
    return rows


def offsets(rows):
    xs, ys, zs = zip(*[(r[1], r[2], r[3]) for r in rows])
    ox, oy, oz = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, (max(zs) + min(zs)) / 2
    print(f"rows={len(rows)}")
    print(f"MAG_OFF_X = {ox:.2f}f, MAG_OFF_Y = {oy:.2f}f, MAG_OFF_Z = {oz:.2f}f")
    print("(valid only if the rotation covered a full 360° in the level plane; "
          f"x span {max(xs)-min(xs):.1f}, y span {max(ys)-min(ys):.1f} uT should be similar)")


def stability(rows):
    tot = [math.sqrt(x * x + y * y + z * z) for _, x, y, z in rows]
    mean = sum(tot) / len(tot)
    sd = math.sqrt(sum((v - mean) ** 2 for v in tot) / len(tot))
    print(f"rows={len(rows)}  |B| mean={mean:.2f} uT  std={sd:.3f} uT  min={min(tot):.2f}  max={max(tot):.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["offsets", "stability"])
    ap.add_argument("csv")
    ap.add_argument("--start", type=int)
    ap.add_argument("--end", type=int)
    a = ap.parse_args()
    rows = load(a.csv, a.start, a.end)
    (offsets if a.mode == "offsets" else stability)(rows)


if __name__ == "__main__":
    main()
