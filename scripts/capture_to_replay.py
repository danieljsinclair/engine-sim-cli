#!/usr/bin/env python3
"""Convert a decoded vehicle-sim capture into an engine-sim --replay-telemetry CSV.

The decoded capture (e.g. foo.txt.csv from vehicle-sim) has columns:
  timestamp_utc_ms, throttle_pct, speed_kmh, ...

This emits an engine-sim replay CSV (time_s, throttle_pct, road_speed_kmh) that
--replay-telemetry --auto consumes: the engine-sim auto gearbox then decides
gears from the REAL road speed, replacing the synthetic DemoVehiclePhysics.

Usage:
  python3 scripts/capture_to_replay.py <decoded_capture.csv> <replay.csv>

Read-only on the source. Drops rows with missing/negative speed (reverse).
"""
import csv
import sys

if len(sys.argv) != 3:
    sys.exit("usage: capture_to_replay.py <decoded_capture.csv> <replay.csv>")

src, out = sys.argv[1], sys.argv[2]
first_ts = None
n_in = n_out = 0
with open(src) as f, open(out, "w", newline="") as o:
    reader = csv.reader(f)
    next(reader, None)  # header
    o.write("time_s,throttle_pct,road_speed_kmh\n")
    for row in reader:
        n_in += 1
        if len(row) < 3:
            continue
        ts, thr, spd = row[0].strip(), row[1].strip(), row[2].strip()
        if not ts.isdigit():
            continue
        ts_i = int(ts)
        if first_ts is None:
            first_ts = ts_i
        try:
            spd_v = float(spd) if spd else None
        except ValueError:
            spd_v = None
        if spd_v is None or spd_v < 0:  # drop reverse / missing (3-point turn)
            continue
        o.write(f"{(ts_i - first_ts) / 1000.0:.3f},{thr or '0'},{spd_v:.2f}\n")
        n_out += 1
print(f"capture_to_replay: {n_in} rows in -> {n_out} rows out -> {out}")
