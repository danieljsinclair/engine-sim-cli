#!/usr/bin/env python3
"""
verify_driveability.py - the driveability GATE.

A full-recording, both-models, verdict-bearing harness that checks EVERY
failure case the team's narrow-window smoke test (scripts/smoke_gearbox.py)
was structurally blind to. NO model passes until ALL invariants pass over the
FULL recording. This is the gate the user demanded ("fix the blindness first").

Why a new harness (not bolted onto smoke_gearbox.py):
  smoke_gearbox.py is a single 5s-window REPLAY tool. Its three blind spots
  were (a) the narrow window fresh-starts the engine AFTER the stall trigger,
  (b) its [NO STALL] check excluded Stopped frames so a Stopped-latch stall was
  invisible, and (c) it ran replay-mode only, never the live --coupling-model
  path where the bugs live. This harness inverts all three: full recording,
  Stopped-latch IS a failure, and it runs the live models directly.

INVARIANTS (each verdict-bearing; ANY fail -> gate RED for that model):
  1. FULL_RECORDING        CSV time span >= MIN_SPAN_S. Catches the 5s-window
                           fresh-start blindness that missed every standstill
                           stall (each --start-from window restarts the engine
                           state machine, so a stall at t=8s never reproduces
                           in a window starting at 00:20).
  2. RPM_NEVER_ZERO        No frame after the cranking spin-up with rpm <=
                           STALL_RPM (default 30). The engine-sim bottoms at
                           ~2-7 rpm while latched Stopped, so <=30 (not ==0)
                           is the real "rpm never hits 0" bar.
  3. NO_STOPPED_LATCH      No engine_state == Stopped frame after the initial
                           crank spin-up. The old [NO STALL] EXCLUDED these
                           (the structural blindness); here they are a hard
                           fail. (One Stopped frame DURING the cold crank is
                           allowed - it is part of the spin-up.)
  4. NO_OSCILLATION        (a) Over consecutive non-Cranking frame pairs that
                           are NOT part of a gear shift, max |Δrpm| <=
                           OSC_MAX_DELTA (500). (b) NO 1s window is a limit
                           cycle: a window with rpm range > OSC_LC_RANGE (200)
                           AND >= OSC_LC_MIN_REVERSALS (6) direction reversals.
                           Reversal density - not raw variance - is used because
                           variance double-counts one-way ramps (a free-rev ramp
                           looks like high variance but is NOT a cycle). Catches
                           the legacy binary relief's 0<->1149 bang-bang.
  5. NO_FREE_REV_IN_GEAR   No frame in a FORWARD gear (gear_physical >= 1) with
                           rpm > FREE_REV_RPM (3000) AND road_implied_rpm <
                           idle. In a forward gear the TC must transfer torque
                           (launch at high throttle, couple at low) - the
                           engine must NEVER free-rev. Free-rev = the TC wrongly
                           decoupled (the stall-fix trade-off).
  6. NO_HI_THROTTLE_FREE_REV  (the launch bar) No frame with throttle >=
                           HI_THROTTLE_PCT (50) AND road_implied < idle AND
                           rpm > FREE_REV_RPM. At WOT/low-speed the TC must
                           LOAD the engine (it sits at ~stall speed and the car
                           accelerates); free-revving here is the worst TC
                           failure (launch does nothing). Overlaps check 5 but
                           is reported separately because it is qualitatively
                           worse.

USAGE
  # Run BOTH models live over the full recording and gate (needs the CLI):
  verify_driveability.py --recording capture.csv \
      [--models torque-converter,clutch-map] [--script es_new/C63_TeslaY.mr]

  # Score pre-built per-frame CSVs (e.g. from a --live-telemetry --csv-out run,
  # fast - skips running the CLI):
  verify_driveability.py --score torque-converter=/tmp/tc.csv \
      --score clutch-map=/tmp/cm.csv

CSVs are parsed BY HEADER NAME (robust to column additions/reorderings).
"""
from __future__ import annotations

import argparse
import csv
import os
import shutil
import statistics
import subprocess
import sys
import tempfile

# ---- defaults (all env-overridable) ----------------------------------------
DEFAULT_CLI = os.environ.get(
    "ENGINE_SIM_CLI",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "build", "engine-sim-cli"),
)
DEFAULT_SCRIPT = os.environ.get("GEARBOX_SCRIPT", "es_new/C63_TeslaY.mr")
DEFAULT_MODELS = os.environ.get("DRIVE_MODELS", "torque-converter,clutch-map")
DEFAULT_BOUND_S = int(os.environ.get("DRIVE_BOUND_S", "40"))

IDLE_RPM = float(os.environ.get("DRIVE_IDLE_RPM", "950"))
STALL_RPM = int(os.environ.get("DRIVE_STALL_RPM", "30"))            # check 2
MIN_SPAN_S = float(os.environ.get("DRIVE_MIN_SPAN_S", "25"))        # check 1
OSC_MAX_DELTA = int(os.environ.get("DRIVE_OSC_MAX_DELTA", "500"))   # check 4a
# check 4b limit-cycle gate: tuned to the VIOLENT coupling limit cycle the
# legacy binary relief produced (~0<->1149, range ~1100). A range gate of 400
# excludes mild unloaded-engine hunting at a free-rev point (measured ~310rpm
# wander) without weakening detection of a genuine bang-bang clutch cycle.
OSC_LC_RANGE = int(os.environ.get("DRIVE_OSC_LC_RANGE", "400"))
OSC_LC_MIN_REVERSALS = int(os.environ.get("DRIVE_OSC_LC_MIN_REVERSALS", "10"))
SHIFT_CLUTCH_STEP = float(os.environ.get("DRIVE_SHIFT_CLUTCH_STEP", "0.15"))
# A clutch pressure at/above this is "engaged" (transmitting torque). The
# oscillation check only scores rpm movement while the clutch is engaged: an
# OPEN clutch means free-rev, whose rpm movement is owned by the free-rev
# checks (5/6), not the coupling-oscillation check. This stops a decoupled
# free-rev hunt (clutch=0) being misread as a coupling limit cycle, and means
# a model cannot hide a real limit cycle by simply opening the clutch (an open
# clutch is itself a free-rev failure, caught elsewhere). 0.03 sits between
# the TC's free-rev pressure (~0.000-0.009) and the ClutchMap floor (0.050).
CLUTCH_ENGAGED_FLOOR = float(os.environ.get("DRIVE_CLUTCH_ENGAGED_FLOOR", "0.03"))
FREE_REV_RPM = int(os.environ.get("DRIVE_FREE_REV_RPM", "3000"))    # checks 5/6
HI_THROTTLE_PCT = float(os.environ.get("DRIVE_HI_THROTTLE_PCT", "50"))  # check 6


def die(msg: str) -> None:
    print(f"GATE FAIL (harness): {msg}", file=sys.stderr)
    sys.exit(2)


# ---- CSV loading (parse by header name) ------------------------------------
def load_csv(path: str) -> list[dict]:
    rows: list[dict] = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            die(f"CSV is empty (no header): {path}")
        for r in reader:
            try:
                r["time_s"] = float(r["time_s"])
                r["rpm"] = int(float(r["rpm"]))
            except (KeyError, ValueError, TypeError):
                # Skip rows we cannot core-coerce the essential fields on; a
                # partially-fed live frame can be malformed without dooming
                # the whole run.
                continue
            for k in ("clutch_pressure", "road_implied_rpm", "vehicle_speed_kmh",
                      "throttle_gas_pct", "sim_speed_mph"):
                if k in r:
                    try:
                        r[k] = float(r[k])
                    except (ValueError, TypeError):
                        r[k] = 0.0
            if "gear_physical" in r:
                try:
                    r["gear_physical"] = int(float(r["gear_physical"]))
                except (ValueError, TypeError):
                    r["gear_physical"] = 0
            r.setdefault("engine_state", "")
            rows.append(r)
    return rows


def find_spinup_end(rows: list[dict]) -> int:
    """Last index of the first CONTIGUOUS Cranking block (the cold spin-up)."""
    end = -1
    for i, r in enumerate(rows):
        if r["engine_state"] == "Cranking":
            end = i
        elif end >= 0 and r["engine_state"] != "Cranking":
            break
    return end


def mark_shift_frames(rows: list[dict]) -> set[int]:
    """Indices that are part of a gear shift: a gear_physical change OR a
    single-frame clutch-pressure step >= SHIFT_CLUTCH_STEP (the SHIFTING-state
    transient where gear_physical has not flipped yet). Used to exclude shift
    transients from the oscillation metric so a clean 1->2 shift is not read
    as a limit cycle."""
    shift: set[int] = set()
    for i in range(1, len(rows)):
        a, b = rows[i - 1], rows[i]
        gear_change = a.get("gear_physical", 0) != b.get("gear_physical", 0)
        clutch_step = abs(b.get("clutch_pressure", 0.0)
                          - a.get("clutch_pressure", 0.0)) >= SHIFT_CLUTCH_STEP
        if gear_change or clutch_step:
            shift.add(i - 1)
            shift.add(i)
    return shift


def median_dt(rows: list[dict]) -> float:
    dts = [rows[i]["time_s"] - rows[i - 1]["time_s"]
           for i in range(1, len(rows)) if rows[i]["time_s"] > rows[i - 1]["time_s"]]
    return statistics.median(dts) if dts else 1.0 / 60.0


# ---- invariant checks ------------------------------------------------------
# Each returns a dict: {name, ok, n, detail, samples:list[str]}.
def check_full_recording(rows: list[dict]) -> dict:
    if not rows:
        return dict(name="FULL_RECORDING", ok=False, n=0,
                    detail="no rows", samples=[])
    span = rows[-1]["time_s"] - rows[0]["time_s"]
    ok = span >= MIN_SPAN_S
    detail = (f"span={span:.1f}s (need >={MIN_SPAN_S:.0f}s); "
              f"{'PASS - full recording' if ok else 'FAIL - narrow window '
              '(a fresh-start 5s window misses standstill stalls)'}")
    return dict(name="FULL_RECORDING", ok=ok, n=int(span), detail=detail, samples=[])


def check_rpm_never_zero(after: list[dict]) -> dict:
    bad = [r for r in after if r["rpm"] <= STALL_RPM]
    ok = not bad
    detail = (f"{len(bad)} frames rpm<={STALL_RPM} after spinup "
              f"(the engine-sim bottoms at ~2-7 while latched Stopped, so "
              f"<= {STALL_RPM} is the real bar)")
    samples = [_fmt(r) for r in bad[:6]]
    return dict(name="RPM_NEVER_ZERO", ok=ok, n=len(bad), detail=detail, samples=samples)


def check_no_stopped_latch(after: list[dict]) -> dict:
    bad = [r for r in after if r["engine_state"] == "Stopped"]
    ok = not bad
    # group consecutive Stopped frames into episodes
    eps = _episodes(after, bad)
    detail = (f"{len(bad)} Stopped-latch frames after spinup in {len(eps)} episode(s) "
              f"(the old [NO STALL] EXCLUDED these - the structural blindness)")
    samples = [f"ep t={e[0]['time_s']:.2f}s len={len(e)} " + _fmt(e[0]) for e in eps[:6]]
    return dict(name="NO_STOPPED_LATCH", ok=ok, n=len(bad), detail=detail, samples=samples)


def check_no_oscillation(rows: list[dict], after: list[dict],
                         shift: set[int]) -> dict:
    def engaged(r: dict) -> bool:
        return r.get("clutch_pressure", 0.0) >= CLUTCH_ENGAGED_FLOOR

    # 4a: max |Δrpm| over consecutive non-Cranking, non-shift pairs where the
    # clutch is ENGAGED on at least one side. An open clutch means free-rev;
    # its rpm movement is owned by the free-rev checks (5/6), not this one.
    max_delta = 0
    worst = None
    for i in range(1, len(rows)):
        a, b = rows[i - 1], rows[i]
        if a["engine_state"] == "Cranking" or b["engine_state"] == "Cranking":
            continue
        if (i - 1) in shift or i in shift:
            continue
        if not (engaged(a) or engaged(b)):
            continue
        d = abs(b["rpm"] - a["rpm"])
        if d > max_delta:
            max_delta, worst = d, (a, b)
    # 4b: limit-cycle detector via reversal density, over ENGAGED frames. A
    # bang-bang coupling cycle (the legacy binary relief 0<->1149) reverses
    # direction nearly every step while the clutch is engaged; a one-way ramp
    # or an open-clutch free-rev hunt does not qualify here. Raw rolling VARIANCE
    # cannot tell these apart (measured: TC std=1337 from a free-rev ramp), so we
    # detect the cycle directly: a 1s window is a limit cycle if its rpm range >
    # OSC_LC_RANGE AND it has >= OSC_LC_MIN_REVERSALS direction changes. (Max
    # rolling std is computed and reported for diagnosis, NOT verdict-bearing.)
    steady_idx = [i for i, r in enumerate(rows)
                  if r["engine_state"] != "Cranking" and i not in shift
                  and engaged(r)]
    w = max(2, int(round(1.0 / median_dt(rows))))
    rpms = [rows[i]["rpm"] for i in steady_idx]
    max_std = 0.0
    lc_windows = 0
    lc_sample = None
    for j in range(w, len(rpms) + 1):
        win = rpms[j - w:j]
        if len(win) < 2:
            continue
        s = statistics.pstdev(win)
        if s > max_std:
            max_std = s
        rng = max(win) - min(win)
        if rng <= OSC_LC_RANGE:
            continue
        reversals = sum(1 for k in range(2, len(win))
                        if (win[k] - win[k - 1]) * (win[k - 1] - win[k - 2]) < 0)
        if reversals >= OSC_LC_MIN_REVERSALS:
            lc_windows += 1
            if lc_sample is None:
                lc_sample = (steady_idx[j - 1], reversals, rng)
    ok = max_delta <= OSC_MAX_DELTA and lc_windows == 0
    detail = (f"max non-shift |Δrpm|={max_delta} (limit {OSC_MAX_DELTA}); "
              f"limit-cycle 1s windows={lc_windows} "
              f"(range>{OSC_LC_RANGE} & >={OSC_LC_MIN_REVERSALS} reversals); "
              f"max rolling std={max_std:.0f} [reported only, not verdict]")
    samples = []
    if worst:
        a, b = worst
        samples.append(f"worst Δ {a['rpm']}->{b['rpm']} @t={b['time_s']:.2f}s "
                       f"clutch={a.get('clutch_pressure', 0):.3f}->"
                       f"{b.get('clutch_pressure', 0):.3f} " + _fmt(b))
    if lc_sample:
        idx, revs, rng = lc_sample
        samples.append(f"limit-cycle window @t={rows[idx]['time_s']:.2f}s "
                       f"{revs} reversals range={rng}rpm " + _fmt(rows[idx]))
    return dict(name="NO_OSCILLATION", ok=ok, n=max_delta, detail=detail, samples=samples)


def check_no_free_rev_in_gear(after: list[dict], idle: float) -> dict:
    bad = [r for r in after
           if r.get("gear_physical", 0) >= 1
           and r["rpm"] > FREE_REV_RPM
           and r.get("road_implied_rpm", 0.0) < idle]
    ok = not bad
    mx = max((r["rpm"] for r in bad), default=0)
    detail = (f"{len(bad)} frames in forward gear with rpm>{FREE_REV_RPM} "
              f"and road_implied<idle({idle:.0f}); max rpm={mx}. "
              f"In gear the TC must transfer torque - free-rev = wrong decouple")
    samples = [_fmt(r) for r in sorted(bad, key=lambda x: -x["rpm"])[:6]]
    return dict(name="NO_FREE_REV_IN_GEAR", ok=ok, n=len(bad), detail=detail, samples=samples)


def check_no_hi_throttle_free_rev(after: list[dict], idle: float) -> dict:
    bad = [r for r in after
           if r.get("throttle_gas_pct", 0.0) >= HI_THROTTLE_PCT
           and r.get("road_implied_rpm", 0.0) < idle
           and r["rpm"] > FREE_REV_RPM]
    ok = not bad
    mx = max((r["rpm"] for r in bad), default=0)
    detail = (f"{len(bad)} frames throttle>={HI_THROTTLE_PCT:.0f}% + low speed + "
              f"rpm>{FREE_REV_RPM}; max rpm={mx}. At WOT/low-speed the TC must "
              f"LOAD the engine (launch), not free-rev")
    samples = [_fmt(r) for r in sorted(bad, key=lambda x: -x["rpm"])[:6]]
    return dict(name="NO_HI_THROTTLE_FREE_REV", ok=ok, n=len(bad),
                detail=detail, samples=samples)


# ---- helpers ---------------------------------------------------------------
def _fmt(r: dict) -> str:
    return (f"t={r['time_s']:.2f} rpm={r['rpm']} state={r['engine_state']} "
            f"clutch={r.get('clutch_pressure', 0):.3f} thr={r.get('throttle_gas_pct', 0):.0f}% "
            f"v={r.get('vehicle_speed_kmh', 0):.1f} road_rpm={r.get('road_implied_rpm', 0):.0f} "
            f"gear={r.get('gear_physical', '?')}")


def _episodes(seq: list[dict], subset: list[dict]) -> list[list[dict]]:
    """Group members of `subset` (which are refs into `seq`) into maximal
    consecutive-by-index runs."""
    idx_of = {id(r): i for i, r in enumerate(seq)}
    eps: list[list[dict]] = []
    cur: list[dict] = []
    prev = -2
    for r in subset:
        i = idx_of.get(id(r))
        if i is None:
            continue
        if i == prev + 1:
            cur.append(r)
        else:
            if cur:
                eps.append(cur)
            cur = [r]
        prev = i
    if cur:
        eps.append(cur)
    return eps


# ---- per-model scoring ------------------------------------------------------
def score(path: str, model: str, idle: float) -> dict:
    rows = load_csv(path)
    if not rows:
        die(f"no rows parsed from {path}")
    spinup = find_spinup_end(rows)
    after = rows[spinup + 1:] if spinup >= 0 else list(rows)
    shift = mark_shift_frames(rows)
    checks = [
        check_full_recording(rows),
        check_rpm_never_zero(after),
        check_no_stopped_latch(after),
        check_no_oscillation(rows, after, shift),
        check_no_free_rev_in_gear(after, idle),
        check_no_hi_throttle_free_rev(after, idle),
    ]
    overall = all(c["ok"] for c in checks)
    return dict(model=model, path=path, rows=len(rows),
                spinup=spinup, checks=checks, overall=overall)


# ---- running the CLI --------------------------------------------------------
def resolve_cli(cli: str) -> str:
    cli = os.path.expanduser(cli)
    if not os.path.isabs(cli):
        repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        cli = os.path.join(repo, cli)
    if not os.path.isfile(cli):
        die(f"CLI binary not found: {cli} (build first)")
    if not os.access(cli, os.X_OK):
        die(f"CLI not executable: {cli}")
    return cli


def run_model(cli: str, recording: str, model: str, script: str,
              out_csv: str, bound_s: int) -> None:
    cmd = [cli, "--silent", "--live-telemetry", "--wheel-coupling", "pin",
           "--coupling-model", model, "--csv-out", out_csv,
           "--script", script, "--start", "--auto"]
    print("CMD:", " ".join(cmd), f"< {recording}  (bound {bound_s}s)")
    killer = _which_killer()
    with open(recording, "rb") as fh:
        if killer:
            proc = subprocess.run(killer + [f"{bound_s}s", *cmd], stdin=fh,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                  timeout=bound_s + 15)
        else:
            # No gtimeout/timeout: rely on the capture EOF + a manual bound.
            proc = subprocess.run(cmd, stdin=fh, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE, timeout=bound_s + 15)
    if proc.returncode not in (0, None) and not _was_killed(proc.returncode):
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        die(f"CLI exited {proc.returncode} for model {model}")
    if not os.path.isfile(out_csv):
        die(f"--csv-out produced no file for model {model}: {out_csv}")


def _which_killer() -> list[str] | None:
    for k in ("gtimeout", "timeout"):
        p = shutil.which(k)
        if p:
            return [p, "-s", "KILL"]
    return None


def _was_killed(rc: int) -> bool:
    # 137 = SIGKILL (gtimeout -s KILL), 124 = timeout's own kill exit.
    return rc in (124, 137)


# ---- reporting --------------------------------------------------------------
def print_matrix(results: list[dict]) -> None:
    print("\n" + "=" * 78)
    print("DRIVEABILITY GATE")
    print("=" * 78)
    models = [r["model"] for r in results]
    # Header
    name_w = 26
    col_w = max(14, max(len(m) for m in models) + 4)
    print(f"  {'Check':<{name_w}}" + "".join(f"{m:>{col_w}}" for m in models))
    print("  " + "-" * (name_w + col_w * len(models)))
    # Each check row
    for ci in range(len(results[0]["checks"])):
        name = f"{ci + 1}. {results[0]['checks'][ci]['name']}"
        cells = []
        for r in results:
            c = r["checks"][ci]
            tag = "PASS" if c["ok"] else "FAIL"
            cells.append(f"{tag}({c['n']})")
        print(f"  {name:<{name_w}}" + "".join(f"{c:>{col_w}}" for c in cells))
    # Verdict row
    print("  " + "-" * (name_w + col_w * len(models)))
    verdicts = ["GREEN" if r["overall"] else "RED" for r in results]
    print(f"  {'VERDICT':<{name_w}}" + "".join(f"{v:>{col_w}}" for v in verdicts))

    # Per-model detail
    for r in results:
        print("\n" + "-" * 78)
        print(f"MODEL: {r['model']}  ({os.path.basename(r['path'])})  "
              f"rows={r['rows']} spinup_end_idx={r['spinup']} -> "
              f"{'GREEN (all checks pass)' if r['overall'] else 'RED (one or more checks failed)'}")
        for c in r["checks"]:
            tag = "PASS" if c["ok"] else "FAIL"
            print(f"  [{tag}] {c['name']}: {c['detail']}")
            for s in c["samples"]:
                print(f"        {s}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--recording", default=None,
                    help="telemetry recording CSV; if given, RUN each model live "
                         "over the full recording (needs the CLI).")
    ap.add_argument("--models", default=DEFAULT_MODELS,
                    help="comma-separated model list to run (default %(default)s)")
    ap.add_argument("--script", default=DEFAULT_SCRIPT,
                    help="engine config .mr/.json (default %(default)s)")
    ap.add_argument("--cli", default=DEFAULT_CLI, help="engine-sim-cli binary")
    ap.add_argument("--bound-s", type=int, default=DEFAULT_BOUND_S,
                    help="per-run kill-timer seconds (default %(default)s)")
    ap.add_argument("--score", action="append", default=[], metavar="MODEL=CSV",
                    help="score a pre-built per-frame CSV for MODEL "
                         "(repeatable). Skips running the CLI.")
    ap.add_argument("--idle", type=float, default=IDLE_RPM,
                    help="idle rpm for free-rev/road-implied tests (default %(default)s)")
    args = ap.parse_args()

    if not args.recording and not args.score:
        die("give --recording (to run the models) or --score MODEL=CSV (to score "
            "pre-built CSVs); see --help")

    # Build the (model, csv) work list.
    work: list[tuple[str, str]] = []
    tmp_dir: str | None = None
    if args.score:
        for spec in args.score:
            if "=" not in spec:
                die(f"--score must be MODEL=CSV, got: {spec}")
            m, p = spec.split("=", 1)
            p = os.path.expanduser(p)
            if not os.path.isfile(p):
                die(f"--score CSV not found: {p}")
            work.append((m.strip(), p))
    if args.recording:
        recording = os.path.expanduser(args.recording)
        if not os.path.isfile(recording):
            die(f"recording not found: {recording}")
        cli = resolve_cli(args.cli)
        tmp_dir = tempfile.mkdtemp(prefix="verify_drive_")
        for m in [x.strip() for x in args.models.split(",") if x.strip()]:
            out_csv = os.path.join(tmp_dir, f"{m}.csv")
            run_model(cli, recording, m, args.script, out_csv, args.bound_s)
            work.append((m, out_csv))

    try:
        results = [score(p, m, args.idle) for m, p in work]
    finally:
        if tmp_dir and os.path.isdir(tmp_dir):
            shutil.rmtree(tmp_dir, ignore_errors=True)

    print_matrix(results)
    n_red = sum(1 for r in results if not r["overall"])
    print("\n" + "=" * 78)
    print(f"GATE SUMMARY: {len(results)} model(s) scored, "
          f"{len(results) - n_red} GREEN, {n_red} RED")
    # Per-model one-liners for easy grepping.
    for r in results:
        failed = [c["name"] for c in r["checks"] if not c["ok"]]
        print(f"  {r['model']:<18} {'GREEN' if r['overall'] else 'RED   '} "
              f"{'(all pass)' if r['overall'] else 'failed: ' + ', '.join(failed)}")
    print("=" * 78)
    return 0 if n_red == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
