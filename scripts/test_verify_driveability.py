#!/usr/bin/env python3
"""
test_verify_driveability.py - synthetic regressions for the NO_OSCILLATION
check against the tach-sensor measurement model.

The gate's rpm column is the FILTERED tach sensor (first-order, tau=0.1s,
matching Simulator::updateFilteredEngineSpeed); rpm_raw is the raw crank.
These tests pin that the measurement model cannot mask the two failure
modes the check exists to catch:

  1. BANG-BANG: the legacy binary relief cycle (0 <-> 1149 rpm at ~5Hz with
     the clutch engaged) must still FAIL the reversal-density metric (4b)
     through the sensor.
  2. ONE-FRAME SNAP: a single-frame 807rpm drop while coupled (the lockup
     snap) must still FAIL the max-delta metric (4a) — which scores rpm_raw,
     because a sensor smears a one-frame step over ~0.3s and a physics
     artifact must fail even when the tach looks smooth.
  3. CONTROL: a smooth locked trace (road-implied tracking, small ripple)
     passes 4 through the same sensor.

Each case synthesizes raw crank rpm, derives the sensor column with the
same discrete filter the simulator uses, writes a CSV, and scores it with
the gate module's own score() — no live CLI, no recordings.

USAGE: python3 scripts/test_verify_driveability.py  (exit 0 = all hold)
"""
from __future__ import annotations

import math
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_driveability as gate  # noqa: E402

DT = 1.0 / 60.0
TAU_S = 0.1  # must match Simulator::updateFilteredEngineSpeed


def sensor(raw: list[float], seed: float = 0.0) -> list[float]:
    """The simulator's discrete first-order filter (alpha = dt/(tau+dt)),
    seeded with the first sample like the C++ implementation."""
    alpha = DT / (TAU_S + DT)
    out: list[float] = []
    f = raw[0] if raw[0] > 0.0 else seed
    for r in raw:
        f += alpha * (r - f)
        out.append(f)
    return out


def write_csv(path: str, raw: list[float]) -> None:
    filt = sensor(raw)
    with open(path, "w") as f:
        f.write("time_s,rpm,rpm_raw,engine_state,throttle_gas_pct,brake,"
                "ignition,gear_selector,gear_auto,gear_physical,"
                "clutch_pressure,road_implied_rpm,creep_relief_fired,"
                "vehicle_speed_kmh,target_speed_kmh,sim_speed_mph,"
                "engine_torque_nm,drivetrain_torque_nm,dyno_torque_nm,"
                "starter_engaged,exhaust_flow_cm3s\n")
        for i, (s, r) in enumerate(zip(filt, raw)):
            t = i * DT
            f.write(f"{t:.4f},{round(s)},{round(r)},Running,30,0.00,1,1,1,3,"
                    f"1.0000,1700.0,0,41.0,41.0,25.6,120.0,900.0,0.0,0,80.0\n")


def osc_verdict(path: str) -> bool:
    result = gate.score(path, "synthetic", idle=950.0)
    by_name = {c["name"]: c for c in result["checks"]}
    return by_name["NO_OSCILLATION"]["ok"]


def case_bang_bang(tmp: str) -> None:
    # 5Hz full-scale relay cycle: the legacy binary relief signature.
    n = 600  # 10s
    raw = [1149.0 if math.sin(2.0 * math.pi * 5.0 * i * DT) >= 0.0 else 0.0
           for i in range(n)]
    p = os.path.join(tmp, "bangbang.csv")
    write_csv(p, raw)
    assert not osc_verdict(p), \
        "BANG-BANG REGRESSION: 0<->1149 at 5Hz passes NO_OSCILLATION " \
        "through the tau=0.1s sensor — the check no longer bites"


def case_one_frame_snap(tmp: str) -> None:
    # Steady locked cruise, ONE frame drops 807rpm (the lockup snap), then a
    # 0.25s convergence back — exactly what the blend was built to remove.
    n = 600
    raw = [1700.0] * n
    raw[300] = 1700.0 - 807.0
    for k in range(1, 40):
        raw[300 + k] = 893.0 + (1700.0 - 893.0) * min(1.0, k / 30.0)
    p = os.path.join(tmp, "snap.csv")
    write_csv(p, raw)
    assert not osc_verdict(p), \
        "SNAP REGRESSION: a one-frame 807rpm drop while coupled passes " \
        "NO_OSCILLATION — 4a on rpm_raw no longer bites"


def case_smooth_control(tmp: str) -> None:
    # Locked cruise tracking road-implied with in-band ripple only.
    n = 600
    raw = [1700.0 + 12.0 * math.sin(2.0 * math.pi * 1.0 * i * DT)
           for i in range(n)]
    p = os.path.join(tmp, "smooth.csv")
    write_csv(p, raw)
    assert osc_verdict(p), \
        "CONTROL REGRESSION: a smooth locked trace FAILS NO_OSCILLATION " \
        "through the sensor — the check is now too strict"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="gate_selftest_") as tmp:
        case_bang_bang(tmp)
        print("[ok] bang-bang 0<->1149 @5Hz still FAILS through the sensor")
        case_one_frame_snap(tmp)
        print("[ok] one-frame 807rpm snap still FAILS on rpm_raw")
        case_smooth_control(tmp)
        print("[ok] smooth locked cruise still PASSES")
    print("ALL SELFTESTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
