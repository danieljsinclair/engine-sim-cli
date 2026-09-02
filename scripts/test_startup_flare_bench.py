#!/usr/bin/env python3
"""
test_startup_flare_bench.py - synthetic regressions for the startup-flare
bench (scripts/startup_flare_bench.py).

The bench gates the trace-faithful crank-throttle contract: on a trace-
driven start the commanded Gas% in the start window (first Cranking frame
through catch + 2s) must stay at the trace's own level, not the synthesized
0.55/0.60 catch floor. These tests pin, against synthetic console-log
fixtures in the REAL frame format (ANSI color codes included) and
synthesized PCM WAVs:

  1. FLARE: the pre-fix character (Cranking at 60%, catch at 55%, trace 5%)
     FAILs on Gas%.
  2. CLEAN: the post-fix character (5% throughout the window) passes.
  3. NO-START: a mid-trace log that never cranks is SKIPPED, not passed —
     the bench must not green-light a run it could not measure.
  4. WAV RAIL: when audio renders, a >0.9 FS burst inside the window FAILs
     and a moderate burst passes; a near-silent file (<1% FS) is
     NONMEASURABLE (the headless silent-render case), never a pass.

USAGE: python3 scripts/test_startup_flare_bench.py  (exit 0 = all hold)
"""
from __future__ import annotations

import os
import struct
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import startup_flare_bench as bench  # noqa: E402

FAILURES: list[str] = []


def check(name: str, fn) -> None:
    try:
        fn()
        print(f"  ok    {name}")
    except AssertionError as e:
        print(f"  FAIL  {name}: {e}")
        FAILURES.append(name)


def frame(t: str, rpm: int, phase: str, gas: int, flow: int) -> str:
    """One console frame in the real format, ANSI codes included (the bench
    must parse captured logs verbatim, and captured logs carry color)."""
    color = {"Stopped": "31", "Cranking": "33", "Running": "32",
             "Idling": "36"}[phase]
    return (
        f"[{t}] [{rpm:6d} RPM] [S:1 I:0] C63_M156_V4 "
        f"\x1b[{color}m{phase}\x1b[0m "
        f"[Gas: {gas:4d}% \x1b[31mB\x1b[0m] [Gear:PMP] "
        f"\x1b[36m[Flow: {flow:+9d} cm3/s]\x1b[0m"
    )


def flaring_log() -> str:
    """Pre-fix character: twin CRANKING_THROTTLE 0.6 / controller 0.55
    override the trace's 0.05 at a standstill start."""
    lines = [
        frame("00:05.000", 0, "Stopped", 60, 0),
        frame("00:05.200", 120, "Cranking", 60, 30000),
        frame("00:06.000", 900, "Cranking", 55, 90000),
        frame("00:08.066", 1375, "Running", 55, 46000),   # catch, still 55
        frame("00:08.500", 1400, "Running", 5, 8000),     # trace re-asserts
        frame("00:09.500", 1350, "Running", 5, 7900),
        frame("00:10.500", 1340, "Running", 5, 8100),
    ]
    return "\n".join(lines)


def clean_log() -> str:
    """Post-fix character: crank and catch at the trace's own 5%."""
    lines = [
        frame("00:07.400", 0, "Stopped", 5, 0),
        frame("00:07.566", 13, "Cranking", 5, 4570),
        frame("00:08.066", 1374, "Running", 5, 11470),    # catch at 5%
        frame("00:09.066", 1300, "Running", 5, 9000),
        frame("00:10.066", 1280, "Running", 5, 9100),
    ]
    return "\n".join(lines)


def wav_header(rate: int, channels: int, n_samples: int) -> bytes:
    data_size = n_samples * channels * 2
    return b"".join([
        b"RIFF", struct.pack("<I", 36 + data_size), b"WAVE",
        b"fmt ", struct.pack("<IHHIIHH", 16, 1, channels, rate,
                             rate * channels * 2, channels * 2, 16),
        b"data", struct.pack("<I", data_size),
    ])


def write_wav(path: str, rate: int, samples: np.ndarray) -> None:
    with open(path, "wb") as fh:
        fh.write(wav_header(rate, 1, samples.size))
        fh.write(samples.astype(np.int16).tobytes())


def test_parse_real_frame_format() -> None:
    frames = bench.parse_frames(flaring_log())
    assert len(frames) == 7, f"expected 7 frames, got {len(frames)}"
    first = frames[0]
    assert first["t"] == 5.0, first
    assert first["phase"] == "Cranking" or first["phase"] == "Stopped", first
    crank = frames[1]
    assert crank["rpm"] == 120 and crank["gas"] == 60, crank
    assert crank["flow"] == 30000, crank
    catch = frames[3]
    assert catch["phase"] == "Running" and catch["gas"] == 55, catch


def test_flare_fails() -> None:
    r = bench.score_log(flaring_log(), max_gas=15)
    assert r["verdict"] == "FAIL", r
    assert r["max_gas"] == 60, r
    assert r["max_gas_phase"] == "Cranking", r
    # window opens at first Cranking (5.2s) and closes 2s after catch (10.066)
    assert abs(r["window_s"][0] - 5.2) < 1e-9, r
    assert abs(r["window_s"][1] - 10.066) < 1e-9, r


def test_clean_passes() -> None:
    r = bench.score_log(clean_log(), max_gas=15)
    assert r["verdict"] == "OK", r
    assert r["max_gas"] == 5, r


def test_no_start_is_skipped() -> None:
    lines = [frame("00:01.000", 2200, "Running", 5, 20000),
             frame("00:02.000", 2250, "Running", 40, 60000)]
    r = bench.score_log("\n".join(lines))
    assert r["verdict"] == "SKIPPED", r


def test_wav_rail_burst_fails() -> None:
    rate = 1000
    n = rate * 10
    samples = np.full(n, 100, dtype=np.int64)
    samples[int(5.3 * rate):int(5.4 * rate)] = 32000   # rail-adjacent burst
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "burst.wav")
        write_wav(path, rate, samples)
        r = bench.score_wav(path, 5.0, 8.0)
    assert r["verdict"] == "WAV-FAIL", r
    assert r["window_peak_fs"] > 0.9, r


def test_wav_moderate_passes() -> None:
    rate = 1000
    n = rate * 10
    samples = np.full(n, 100, dtype=np.int64)
    samples[int(5.3 * rate):int(5.4 * rate)] = 20000   # loud but sub-rail
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "moderate.wav")
        write_wav(path, rate, samples)
        r = bench.score_wav(path, 5.0, 8.0)
    assert r["verdict"] == "WAV-OK", r


def test_wav_silent_render_is_nonmeasurable() -> None:
    rate = 1000
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "silent.wav")
        write_wav(path, rate, np.zeros(rate * 10, dtype=np.int64))
        r = bench.score_wav(path, 0.0, 10.0)
    assert r["verdict"] == "WAV-NONMEASURABLE", r
    assert "silent" in r["reason"], r


def main() -> int:
    print("startup_flare_bench synthetic regressions:")
    check("parse: real frame format (ANSI)", test_parse_real_frame_format)
    check("gas: pre-fix flare FAILs", test_flare_fails)
    check("gas: trace-faithful start passes", test_clean_passes)
    check("gas: mid-trace log is SKIPPED not OK", test_no_start_is_skipped)
    check("wav: rail burst in window FAILs", test_wav_rail_burst_fails)
    check("wav: moderate burst passes", test_wav_moderate_passes)
    check("wav: silent render NONMEASURABLE", test_wav_silent_render_is_nonmeasurable)

    if FAILURES:
        print(f"\n{len(FAILURES)} FAILED: {', '.join(FAILURES)}")
        return 1
    print("\nall startup-flare bench regressions hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
