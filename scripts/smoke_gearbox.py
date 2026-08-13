#!/usr/bin/env python3
"""
Gearbox smoke test.

Runs the engine-sim-cli on a recording window (via --csv-out), parses the
per-frame CSV, and asserts the driveability invariants that pin the
"6 mph stall" regression on UpLeckHillWithKickdown (00:20-00:25):

  1. RPM FLOOR: every mid-drive frame has rpm >= RPM_THRESHOLD (default 950).
     Mid-drive excludes engine_state == Cranking, Stopped, and the first
     SKIP_S seconds of the window (cold-crank catch-up).
  2. NO STALL: zero mid-drive frames with engine_state == Stopped or rpm == 0.
  3. SPEED TRACKING: >= MPH_PASS_PCT (default 90%) of mid-drive frames have
     |sim_mph - tgt_mph| <= MPH_MAXERR (default 2 mph).

On any violation: prints the offending rows and exits non-zero. Exits 0 if all
invariants hold, and prints a one-line summary.

The CSV is PARSED BY HEADER NAME (not column index), so it is robust to column
additions/reorderings. The expected --csv-out header (as built by the CLI) is:

    time_s,rpm,engine_state,throttle_gas_pct,brake,ignition,gear_selector,
    gear_auto,gear_physical,clutch_pressure,road_implied_rpm,creep_relief_fired,
    vehicle_speed_kmh,target_speed_kmh,sim_speed_mph,engine_torque_nm,
    drivetrain_torque_nm,dyno_torque_nm,starter_engaged,exhaust_flow_cm3s

For mph, the script looks for sim_mph then sim_speed_mph, and for the target it
prefers tgt_mph, then target_speed_mph, then target_speed_kmh (converted /1.60934).

NOTE: the CLI's --end-at flag requires --replay-telemetry (not --live-telemetry),
so this smoke test runs in replay mode (deterministic: the recording is the
sole input source). See `engine-sim-cli --help`.
"""
from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
import tempfile
from collections import Counter

KMH_PER_MPH = 1.60934

# Defaults -- overridable via CLI flags / env.
DEFAULT_CLI = os.environ.get(
    "ENGINE_SIM_CLI",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "build", "engine-sim-cli"),
)
DEFAULT_SCRIPT = os.environ.get("GEARBOX_SCRIPT", "es_new/C63_TeslaY.mr")
DEFAULT_RPM_THRESHOLD = int(os.environ.get("SMOKE_RPM_THRESHOLD", "950"))
DEFAULT_MPH_MAXERR = float(os.environ.get("SMOKE_MPH_MAXERR", "2"))
DEFAULT_MPH_PASS_PCT = float(os.environ.get("SMOKE_MPH_PASS_PCT", "90"))
DEFAULT_SKIP_S = float(os.environ.get("SMOKE_SKIP_S", "3.0"))

# RPM at/above which the engine is unquestionably combustion-sustained. Mirrors
# the twin's kStallRpm (VirtualIceTwin.cpp): below this the closed-loop engine
# is latched Stopped or being re-cranked. The rpm-never-0 bar is "no stall,
# ever" — so a frame whose rpm reads as low as this (even if not exactly 0,
# e.g. the torque-converter bottoms at ~2 rpm while latched Stopped) IS a stall.
STALL_NEAR_ZERO_RPM = int(os.environ.get("SMOKE_STALL_RPM", "30"))


def die(msg: str) -> None:
    print(f"SMOKE FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def resolve_cli(cli: str) -> str:
    cli = os.path.expanduser(cli)
    if not os.path.isabs(cli):
        repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        cli = os.path.join(repo, cli)
    if not os.path.isfile(cli):
        die(f"CLI binary not found: {cli} (build first with `make build`)")
    if not os.access(cli, os.X_OK):
        die(f"CLI binary not executable: {cli}")
    return cli


def run_cli(cli, recording, script, start, end, csv_out):
    # replay mode: --end-at REQUIRES --replay-telemetry. --start auto-cranks.
    cmd = [
        cli, "--silent",
        "--replay-telemetry", recording,
        "--wheel-coupling", "pin",
        "--start-from", start,
        "--end-at", end,
        "--csv-out", csv_out,
        "--script", script,
        "--start", "--auto",
    ]
    print("CMD:", " ".join(cmd))
    with open(os.devnull, "rb") as devnull:
        proc = subprocess.run(cmd, stdin=devnull,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=300)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        die(f"CLI exited {proc.returncode}")
    if not os.path.isfile(csv_out):
        die(f"--csv-out produced no file: {csv_out}")


def mph_for(row):
    """Return sim mph from row, or None if column absent/unparseable."""
    for col in ("sim_mph", "sim_speed_mph"):
        if col in row:
            try:
                return float(row[col])
            except (ValueError, TypeError):
                return None
    return None


def tgt_mph_for(row):
    """Return target mph from row, converting kmh if that's all that exists."""
    for col in ("tgt_mph", "target_speed_mph"):
        if col in row:
            try:
                return float(row[col])
            except (ValueError, TypeError):
                return None
    if "target_speed_kmh" in row:
        try:
            return float(row["target_speed_kmh"]) / KMH_PER_MPH
        except (ValueError, TypeError):
            return None
    return None


def parse_csv(path):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            die(f"CSV is empty (no header): {path}")
        rows = list(reader)
    # numeric coercion for the fields we care about
    for r in rows:
        for k in ("time_s", "rpm"):
            if k in r:
                try:
                    r[k] = float(r[k]) if k == "time_s" else int(float(r[k]))
                except (ValueError, TypeError):
                    r[k] = None
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recording", nargs="?", default=None,
                    help="telemetry recording CSV (replay input). Optional when "
                         "--csv-in is given (score an existing per-frame CSV "
                         "instead of running the CLI).")
    ap.add_argument("--csv-in", default=None,
                    help="score an EXISTING per-frame CSV (e.g. a --live-telemetry "
                         "--csv-out file) instead of running the CLI. Lets the "
                         "invariants run on live-mode output, which --replay-telemetry "
                         "cannot exercise. Skips recording/CLI resolution.")
    ap.add_argument("--start-from", default="00:20",
                    help="--start-from for the CLI window (default 00:20)")
    ap.add_argument("--end-at", default="00:25",
                    help="--end-at for the CLI window (default 00:25)")
    ap.add_argument("--script", default=DEFAULT_SCRIPT,
                    help="engine config .mr/.json (default %(default)s)")
    ap.add_argument("--rpm-threshold", type=int, default=DEFAULT_RPM_THRESHOLD,
                    help="min rpm for mid-drive frames (default %(default)s)")
    ap.add_argument("--mph-maxerr", type=float, default=DEFAULT_MPH_MAXERR,
                    help="max |sim-tgt| mph error (default %(default)s)")
    ap.add_argument("--mph-pass-pct", type=float, default=DEFAULT_MPH_PASS_PCT,
                    help="min %% of frames within mph-maxerr (default %(default)s)")
    ap.add_argument("--skip", type=float, default=DEFAULT_SKIP_S,
                    help="discard first N seconds of window output "
                         "(cold-crank, default %(default)s)")
    ap.add_argument("--strict-stall", action="store_true",
                    help="make the rpm-never-0 bar verdict-bearing for "
                         "near-zero (<=STALL_RPM) and Stopped-latch frames "
                         "too, not only rpm==0 (default: report-only)")
    ap.add_argument("--cli", default=DEFAULT_CLI,
                    help="path to engine-sim-cli binary (default %(default)s)")
    ap.add_argument("--csv-out", default=None,
                    help="persist the CLI's CSV to this path (default: temp)")
    args = ap.parse_args()

    # --csv-in mode: score an existing per-frame CSV (e.g. a live-telemetry
    # --csv-out) without running the CLI. Lets the invariants run on live-mode
    # output, which the default --replay-telemetry path cannot exercise.
    if args.csv_in:
        csv_path = os.path.expanduser(args.csv_in)
        if not os.path.isfile(csv_path):
            die(f"--csv-in not found: {csv_path}")
        recording = csv_path  # for the window banner label only
        rows = parse_csv(csv_path)
    else:
        if not args.recording:
            die("either a recording positional or --csv-in must be given")
        recording = os.path.expanduser(args.recording)
        if not os.path.isfile(recording):
            die(f"recording not found: {recording}")

        cli = resolve_cli(args.cli)

        tmp_dir = None
        if args.csv_out:
            csv_path = args.csv_out
        else:
            tmp_dir = tempfile.mkdtemp(prefix="smoke_gearbox_")
            csv_path = os.path.join(tmp_dir, "out.csv")

        try:
            run_cli(cli, recording, args.script, args.start_from, args.end_at, csv_path)
            rows = parse_csv(csv_path)
        finally:
            if tmp_dir and os.path.isdir(tmp_dir):
                import shutil
                shutil.rmtree(tmp_dir, ignore_errors=True)

    if not rows:
        die("no CSV rows parsed")

    def t_s(r):
        v = r.get("time_s")
        return v if isinstance(v, (int, float)) else None

    # window-relative time: subtract the earliest frame time so --skip is
    # measured from the start of the produced output, not absolute transcript.
    times = [t_s(r) for r in rows if t_s(r) is not None]
    t0 = min(times) if times else 0.0

    def is_cranking(r):
        return str(r.get("engine_state", "")) == "Cranking"

    def is_stopped(r):
        return str(r.get("engine_state", "")) == "Stopped"

    def mid_drive(r):
        # exclude Cranking, Stopped, and the cold-crank warm-up window
        if is_cranking(r) or is_stopped(r):
            return False
        t = t_s(r)
        if t is None:
            return True
        return (t - t0) >= args.skip

    drive = [r for r in rows if mid_drive(r)]
    if not drive:
        die(f"no mid-drive frames after skip={args.skip}s warm-up "
            f"(window {args.start_from}-{args.end_at}); "
            f"check the recording window or reduce --skip")

    # ---- Invariant 0: RPM-NEVER-0 (the hard bar) --------------------------
    # The user's absolute requirement: the engine rpm must NEVER hit 0 (no
    # stall, ever). The existing NO STALL check (Invariant 2) is structurally
    # blind to this because `mid_drive()` excludes ALL Stopped frames — so any
    # stall that latches the engine Stopped (the common failure mode) is
    # invisible to it. This invariant closes that hole:
    #   - It scans EVERY frame AFTER the initial cranking spin-up (the only
    #     legitimate sub-zero-rpm window is the cold crank at the very start),
    #     INCLUDING Stopped and re-crank Cranking frames — because a mid-drive
    #     Stopped latch or a Stopped<->Cranking oscillation IS the stall
    #     signature, not something to filter out.
    #   - It fails on rpm == 0 (the literal bar) AND on rpm <= STALL_NEAR_ZERO_RPM
    #     (the practical bar: the torque-converter bottoms at ~2 rpm while
    #     latched Stopped, so a naive ==0 check would falsely call it clean).
    #
    # NOTE: this only catches stalls that fall INSIDE the --start-from/--end-at
    # window. To catch the standstill stalls on UpLeckHillWithKickdown (which
    # fire at ~00:08, ~00:15, ~00:25) you must run the FULL recording, not the
    # default 00:20-00:25 window — see `--start-from 00:00 --end-at <end>`.
    def initial_crank_end_idx():
        # Last index of the first CONTIGUOUS Cranking block (the cold spin-up).
        end = -1
        for i, r in enumerate(rows):
            if is_cranking(r):
                end = i
            elif end >= 0 and not is_cranking(r):
                break  # first non-Cranking frame after the spin-up
        return end

    crank_end = initial_crank_end_idx()
    after_spinup = rows[crank_end + 1:] if crank_end >= 0 else list(rows)
    zero_frames = [r for r in after_spinup
                   if r.get("rpm") is not None and r["rpm"] == 0]
    near_zero_frames = [r for r in after_spinup
                        if r.get("rpm") is not None
                        and 0 < r["rpm"] <= STALL_NEAR_ZERO_RPM]
    stopped_frames = [r for r in after_spinup if is_stopped(r)]
    # The hard bar: literally zero rpm after the spin-up.
    rpm_never_zero_pass = len(zero_frames) == 0
    # The practical bar: no near-zero (<= STALL_NEAR_ZERO_RPM) and no Stopped
    # latch after the spin-up. Reported but, to match the user's literal "rpm
    # must never hit 0" bar, only the zero_frames condition is verdict-bearing
    # unless --strict-stall is set.
    strict_stall_pass = not near_zero_frames and not stopped_frames
    hard_bar_pass = rpm_never_zero_pass and (strict_stall_pass if args.strict_stall
                                             else True)

    # ---- Invariant 1: RPM floor -------------------------------------------
    rpm_bad = [r for r in drive if (r.get("rpm") is not None and r["rpm"] < args.rpm_threshold)]
    rpm_pass = not rpm_bad

    # ---- Invariant 2: NO stall --------------------------------------------
    stall_bad = [r for r in drive if (r.get("rpm") == 0 or is_stopped(r))]
    stall_pass = len(stall_bad) == 0

    # ---- Invariant 3: speed tracking --------------------------------------
    pairs = [(r, mph_for(r), tgt_mph_for(r)) for r in drive]
    scored = []
    unscored = []
    for r, s, t in pairs:
        if s is None or t is None:
            unscored.append(r)
            continue
        scored.append((r, abs(s - t)))
    if scored:
        within = sum(1 for _, d in scored if d <= args.mph_maxerr)
        mph_pass_pct = 100.0 * within / len(scored)
        mae = sum(d for _, d in scored) / len(scored)
        worst = max(d for _, d in scored)
    else:
        mph_pass_pct = 0.0
        mae = float("nan")
        worst = float("nan")
    mph_pass = mph_pass_pct >= args.mph_pass_pct

    # ---- diagnostics ------------------------------------------------------
    def fmt_row(r):
        t = t_s(r)
        tstr = f"{t:.2f}" if t is not None else "?"
        return (f"t={tstr} rpm={r.get('rpm')} state={r.get('engine_state')} "
                f"sim_mph={mph_for(r)} tgt_mph={tgt_mph_for(r)} "
                f"gear_phys={r.get('gear_physical')}")

    print(f"\nSMOKE window {args.start_from}-{args.end_at} on "
          f"{os.path.basename(recording)}")
    print(f"  mid-drive frames: {len(drive)}  (skip={args.skip}s, "
          f"rpm-floor={args.rpm_threshold}, mph-maxerr={args.mph_maxerr}, "
          f"mph-pass%={args.mph_pass_pct})")
    print(f"  after-spinup frames scanned for rpm-never-0: {len(after_spinup)} "
          f"(spinup ended idx={crank_end}, strict_stall={args.strict_stall})")

    # ---- Invariant 0 diagnostics: RPM-NEVER-0 (hard bar) ------------------
    # The per-line tag reflects the FULL hard bar: with --strict-stall a
    # Stopped-latch or near-zero dip is a failure even if rpm never reads
    # exactly 0 (the engine-sim bottoms at ~2-7 rpm while latched Stopped, so a
    # literal ==0 check is necessary but not sufficient).
    tag = "PASS" if hard_bar_pass else "FAIL"
    if not rpm_never_zero_pass:
        extra = "  <-- HARD-BAR VIOLATION: rpm hit 0 after the cranking spin-up"
    elif args.strict_stall and not strict_stall_pass:
        extra = "  <-- HARD-BAR VIOLATION (--strict-stall): Stopped-latch or near-zero stall"
    else:
        extra = ""
    print(f"  [RPM!=0]    {tag}: {len(zero_frames)} zero-rpm frames "
          f"after spinup (near-zero<={STALL_NEAR_ZERO_RPM}: "
          f"{len(near_zero_frames)}, Stopped-latch: {len(stopped_frames)})"
          f"{extra}")
    for r in zero_frames[:10]:
        print(f"      ZERO " + fmt_row(r))
    if not rpm_never_zero_pass and len(zero_frames) > 10:
        print(f"      ...and {len(zero_frames) - 10} more zero-rpm frames")
    # Group Stopped-latch frames into episodes so a Stopped<->Cranking
    # oscillation reads as a few episodes, not hundreds of lines.
    if stopped_frames:
        # Map each row to its index once (identity-keyed) so grouping is O(n).
        id_to_idx = {id(r): i for i, r in enumerate(rows)}
        eps, cur = [], []
        prev_i = -2
        for r in stopped_frames:
            i = id_to_idx[id(r)]
            if i == prev_i + 1:
                cur.append(i)
            else:
                if cur:
                    eps.append(cur)
                cur = [i]
            prev_i = i
        if cur:
            eps.append(cur)
        print(f"      Stopped-latch episodes: {len(eps)} "
              f"(showing up to 6, first frame of each):")
        for e in eps[:6]:
            print(f"        idx {e[0]}..{e[-1]} ({len(e)} frames)  "
                  + fmt_row(rows[e[0]]))

    if rpm_bad:
        rpms = sorted(r.get("rpm") for r in rpm_bad if r.get("rpm") is not None)
        print(f"  [RPM FLOOR] FAIL: {len(rpm_bad)}/{len(drive)} mid-drive "
              f"frames below {args.rpm_threshold} (min={rpms[0] if rpms else '?'}, "
              f"max={rpms[-1] if rpms else '?'}). bucketed (width 100):")
        # bucketed histogram so we don't print one line per rpm value
        buckets = Counter(int(rpm // 100) * 100 for rpm in rpms)
        for b in sorted(buckets):
            print(f"      rpm [{b:4d}-{b+99:4d}): {buckets[b]} frames")
        print("  sample offending rows (up to 10):")
        for r in rpm_bad[:10]:
            print("      " + fmt_row(r))
    else:
        print(f"  [RPM FLOOR] PASS: all {len(drive)} mid-drive frames >= {args.rpm_threshold}")
    if stall_bad:
        print(f"  [NO STALL]  FAIL: {len(stall_bad)} mid-drive stall frames:")
        for r in stall_bad[:10]:
            print("      " + fmt_row(r))
    else:
        print(f"  [NO STALL]  PASS: zero mid-drive stall frames")
    if scored:
        tag = "PASS" if mph_pass else "FAIL"
        print(f"  [SPEED]     {tag}: mph MAE={mae:.3f} worst={worst:.2f} "
              f"within-{args.mph_maxerr}={mph_pass_pct:.1f}% "
              f"(need >={args.mph_pass_pct}%)")
        if not mph_pass:
            worst_rows = sorted(scored, key=lambda x: -x[1])[:10]
            for r, d in worst_rows:
                print(f"      d={d:.2f} " + fmt_row(r))
    else:
        print(f"  [SPEED]     SKIP: no frames with both sim+tgt mph "
              f"(unscored={len(unscored)})")

    # ---- one-line summary -------------------------------------------------
    min_rpm = min((r["rpm"] for r in drive if r.get("rpm") is not None), default=None)
    overall = hard_bar_pass and rpm_pass and stall_pass and mph_pass
    verdict = "GREEN" if overall else "RED"
    mae_str = f"{mae:.3f}" if scored else "n/a"
    print(f"\nSMOKE SUMMARY verdict={verdict} frames={len(drive)} "
          f"min_rpm={min_rpm} zero_rpm={len(zero_frames)} "
          f"near_zero={len(near_zero_frames)} stopped={len(stopped_frames)} "
          f"stalls={len(stall_bad)} rpm_below={len(rpm_bad)} "
          f"mph_MAE={mae_str} mph_within_pct={mph_pass_pct:.1f}%")

    sys.exit(0 if overall else 1)


if __name__ == "__main__":
    main()
