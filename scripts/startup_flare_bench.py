#!/usr/bin/env python3
"""
startup_flare_bench.py - gate the startup-flare regression on replay starts.

DEFECT THIS GATES (fixed 2026-09-03, trace-driven crank throttle): on a
telemetry run with an early start point the crank path synthesized its own
start character — CRANKING_THROTTLE (0.6 twin / 0.55 controller) overrode the
trace's near-zero throttle at a standstill start, and the unloaded ignition
catch flared to full scale (the owner-reported startup crackle). The fix
floors the crank throttle at the trace value when the run is trace-driven;
scripted/interactive starts keep the absolute catch-guarantee floor.

OPERATIVE METRIC (log-level): parse the console log's simulation frames and
measure the commanded Gas% over the start window — first Cranking frame
through catch (first Running frame) + 2s. On a standstill-start capture whose
trace throttle is ~0, any synthesized throttle above the idle-sustain floor
(5%) is the flare. Default fail threshold 15% (idle floor + margin). A mid-
trace start (engine already running, no Cranking frames in the window) has
nothing to gate and is reported SKIPPED.

WAV METRIC (when --wav is given): peak sample amplitude within the same
window must stay under 0.9 FS. CAVEAT (measured 2026-09-03 on the
PinFixDrive3 standstill-start capture, both pre-fix and post-fix
binaries): the synth's master output saturates around 0.5 FS, so normal
loud audio and the flare BOTH ride that ceiling — the WAV peak cannot
discriminate this defect (before/after both measured 50.0% FS in-window;
the discriminator is the Gas%/flow numbers above). The rail check stays
as a structural guard, and a header-only or <1% FS file is reported
WAV-NONMEASURABLE rather than passed.

USAGE:
  score a captured log:
    python3 scripts/startup_flare_bench.py --log run.log [--max-gas 15]
  score log + wav:
    python3 scripts/startup_flare_bench.py --log run.log --wav run.wav
Unit regressions: scripts/test_startup_flare_bench.py (synthetic fixtures,
no live CLI, no recordings).
"""
from __future__ import annotations

import argparse
import re
import sys

# ANSI color escapes wrap the state word and other fields in the console log.
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# Frame shape (ANSI-stripped):
# [00:07.566] [   13 RPM] [S:1 I:0] C63_M156_V4 Cranking [Gas:   5% B] ...
#   [Flow:     +4570 cm3/s] ...
FRAME_RE = re.compile(
    r"\[(\d{1,2}):(\d{2}\.\d{3})\]\s+"  # [MM:SS.mmm]
    r"\[\s*(\d+)\s*RPM\].*?"           # [  NNN RPM]
    r"\b(Stopped|Cranking|Running|Idling)\b.*?"  # engine phase
    r"\[Gas:\s*(\d+)%"                 # [Gas:  N% ...] (trailing B/P flags)
)

FLOW_RE = re.compile(r"\[\s*Flow:\s*([+-]?\d+)\s*cm3/s\]")

# WAV peak over the full-scale rail: 0.9 FS. The pre-fix flare hit 16383.
FS_PEAK_LIMIT = 0.9
# A WAV whose overall peak is below this carries no flare signal (the
# headless silent-render case) — nonmeasurable, not a pass.
WAV_MEASURABLE_FS = 0.01


def parse_frames(log_text: str) -> list[dict]:
    """Extract (time_s, rpm, phase, gas, flow) frames from a console log."""
    frames = []
    for line in ANSI_RE.sub("", log_text).splitlines():
        m = FRAME_RE.search(line)
        if not m:
            continue
        minutes, seconds, rpm, phase, gas = m.groups()
        f = re.search(r"\[Gas:\s*\d+%.*\[\s*Flow:\s*([+-]?\d+)\s*cm3/s\]",
                      line)
        flow = int(f.group(1)) if f else None
        frames.append({
            "t": int(minutes) * 60.0 + float(seconds),
            "rpm": int(rpm),
            "phase": phase,
            "gas": int(gas),
            "flow": flow,
        })
    return frames


def start_window(frames: list[dict], catch_hold_s: float = 2.0):
    """(window_start, catch_t) for the first Cranking -> Running sequence,
    or None when no start event exists in the log (mid-trace start)."""
    crank_t = next((f["t"] for f in frames if f["phase"] == "Cranking"), None)
    if crank_t is None:
        return None
    catch_t = next((f["t"] for f in frames
                    if f["phase"] == "Running" and f["t"] >= crank_t), None)
    if catch_t is None:
        return None
    return crank_t, catch_t + catch_hold_s


def score_log(log_text: str, max_gas: int = 15) -> dict:
    """Verdict on the start window's commanded Gas%. FAIL = flare; OK =
    trace-faithful; SKIPPED = no start event in the log."""
    frames = parse_frames(log_text)
    win = start_window(frames)
    if win is None:
        return {"verdict": "SKIPPED",
                "reason": "no Cranking->Running start event in log",
                "frames": len(frames)}

    start_t, end_t = win
    window = [f for f in frames if start_t <= f["t"] <= end_t]
    max_gas_frame = max(window, key=lambda f: f["gas"])
    catch_t = next(f["t"] for f in frames
                   if f["phase"] == "Running" and f["t"] >= start_t)
    post = [f for f in frames if catch_t + 0.5 <= f["t"] <= end_t
            and f["flow"] is not None and f["flow"] > 0]
    flare_flow_ratio = None
    if post:
        max_flow = max(abs(f["flow"]) for f in window if f["flow"] is not None)
        flare_flow_ratio = max_flow / min(f["flow"] for f in post)

    verdict = "FAIL" if max_gas_frame["gas"] > max_gas else "OK"
    return {
        "verdict": verdict,
        "frames": len(frames),
        "window_s": (start_t, end_t),
        "max_gas": max_gas_frame["gas"],
        "max_gas_at": max_gas_frame["t"],
        "max_gas_phase": max_gas_frame["phase"],
        "flare_flow_ratio": flare_flow_ratio,
        "threshold_gas": max_gas,
    }


def score_wav(wav_path: str, start_t: float, end_t: float) -> dict:
    """Peak amplitude over the start window, read from the PCM samples.

    Returns verdict WAV-OK / WAV-FAIL / WAV-NONMEASURABLE. Nonmeasurable =
    the whole file is near-silent (<1% FS): the headless silent-render case
    where the WAV carries no flare signal and the log metric is the gate.
    """
    import struct

    import numpy as np

    data = open(wav_path, "rb").read()
    if len(data) < 44:
        return {"verdict": "WAV-NONMEASURABLE", "reason": "header-only WAV"}
    rate = struct.unpack("<I", data[24:28])[0]
    channels = struct.unpack("<H", data[22:24])[0] or 2
    left = np.frombuffer(data[44:], dtype=np.int16)[0::channels]
    if left.size == 0:
        return {"verdict": "WAV-NONMEASURABLE", "reason": "no samples"}

    fs = 32768.0
    overall_peak = float(np.max(np.abs(left.astype(np.float64)))) / fs
    if overall_peak < WAV_MEASURABLE_FS:
        return {"verdict": "WAV-NONMEASURABLE",
                "reason": f"file peak {overall_peak:.3%} FS < "
                          f"{WAV_MEASURABLE_FS:.0%} (silent render)"}

    i0 = max(0, int(start_t * rate))
    i1 = min(left.size, int(end_t * rate))
    if i1 <= i0:
        return {"verdict": "WAV-NONMEASURABLE", "reason": "window off-WAV"}
    peak = float(np.max(np.abs(left[i0:i1].astype(np.float64)))) / fs
    verdict = "WAV-FAIL" if peak > FS_PEAK_LIMIT else "WAV-OK"
    return {"verdict": verdict, "window_peak_fs": peak,
            "limit_fs": FS_PEAK_LIMIT, "rate": rate}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--log", required=True, help="captured console log")
    ap.add_argument("--wav", help="optional --output WAV for the rail check")
    ap.add_argument("--max-gas", type=int, default=15,
                    help="FAIL if start-window Gas%% exceeds this (default 15)")
    args = ap.parse_args()

    result = score_log(open(args.log, encoding="utf-8", errors="replace").read(),
                       args.max_gas)
    print(f"frames parsed      : {result['frames']}")
    if result["verdict"] == "SKIPPED":
        print(f"verdict            : SKIPPED ({result['reason']})")
        return 0
    print(f"start window       : {result['window_s'][0]:.3f}s .. "
          f"{result['window_s'][1]:.3f}s")
    print(f"max Gas in window  : {result['max_gas']}% @ "
          f"{result['max_gas_at']:.3f}s ({result['max_gas_phase']})")
    print(f"flare flow ratio   : "
          f"{result['flare_flow_ratio'] if result['flare_flow_ratio'] is not None else 'n/a'}")
    print(f"GAS verdict        : {result['verdict']} "
          f"(threshold {result['threshold_gas']}%)")

    exit_code = 0 if result["verdict"] == "OK" else 1

    if args.wav:
        wav = score_wav(args.wav, *result["window_s"])
        if "reason" in wav:
            print(f"WAV verdict        : {wav['verdict']} ({wav['reason']})")
        else:
            print(f"WAV verdict        : {wav['verdict']} "
                  f"(window peak {wav['window_peak_fs']:.1%} FS, "
                  f"limit {wav['limit_fs']:.0%})")
        if wav["verdict"] == "WAV-FAIL":
            exit_code = 1

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
