#!/usr/bin/env python3
"""
sine_knock_smoke.py — CI smoke test for the sync-pull audio knock.

Renders a --sine --silent run to a WAV (the true rendered output, captured
pre-silent-mute by the --output seam) and measures buffer-boundary
discontinuities. The knock is a mid-waveform restart from the audio ring's
write index lapping its read index; it shows up as a large int16 jump between
adjacent samples. A clean sine has at most a handful of startup-transient
jumps; the knock produces hundreds.

Usage: sine_knock_smoke.py <wav_path> [--threshold-disc N] [--threshold-uf-pct F]

Exit 0 = PASS, exit 1 = FAIL (knock present).
"""
import argparse
import numpy as np
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", help="Path to the rendered WAV file")
    ap.add_argument("--threshold-disc", type=int, default=10,
                    help="FAIL if discontinuities exceed this (default 10)")
    ap.add_argument("--threshold-uf-pct", type=float, default=1.0,
                    help="FAIL if silent-sample rate exceeds this pct (default 1.0)")
    ap.add_argument("--jump", type=int, default=5000,
                    help="abs(diff) counted as a discontinuity (default 5000)")
    args = ap.parse_args()

    data = open(args.wav, "rb").read()
    if len(data) < 44:
        print("FAIL: WAV too short")
        return 1

    # Parse WAV header: samples start at byte 44 for a basic PCM header.
    # (The CLI's WavWriter writes a 44-byte header.)
    all_samples = np.frombuffer(data[44:], dtype=np.int16)
    if all_samples.size == 0:
        print("FAIL: no audio samples in WAV")
        return 1

    # Stereo interleaved: the knock is mono-sourced, measure the left channel.
    left = all_samples[0::2].astype(np.int32)

    # Discontinuities: adjacent-sample jumps larger than a sine slew allows.
    diffs = np.abs(np.diff(left))
    discontinuities = int(np.sum(diffs > args.jump))

    # Underflow proxy: the knock's zero-sample dropouts and the pre-fix
    # silence-fills both read as runs of exact-zero samples. A healthy 440Hz
    # sine at int16 scale crosses zero twice per cycle but almost never SITS
    # on exactly zero across a full frame. Count exact-zero samples as a
    # conservative underflow/silence proxy.
    zero_samples = int(np.sum(left == 0))
    zero_rate = 100.0 * zero_samples / left.size

    # RMS sanity: a silent or collapsed output is itself a failure.
    rms = float(np.sqrt(np.mean(left.astype(np.float64) ** 2)))

    print(f"samples={left.size} rms={rms:.1f}")
    print(f"discontinuities(>{args.jump})={discontinuities}  threshold={args.threshold_disc}")
    print(f"zero_samples={zero_samples} rate={zero_rate:.3f}%  threshold={args.threshold_uf_pct}%")

    failed = False
    if discontinuities > args.threshold_disc:
        print(f"FAIL: discontinuities {discontinuities} > {args.threshold_disc}")
        failed = True
    if zero_rate > args.threshold_uf_pct:
        print(f"FAIL: zero-sample rate {zero_rate:.3f}% > {args.threshold_uf_pct}%")
        failed = True
    if rms < 1000.0:
        print(f"FAIL: RMS {rms:.1f} < 1000 (silent/collapsed output)")
        failed = True

    if failed:
        print("VERDICT: FAIL (knock present)")
        return 1
    print("VERDICT: PASS (no knock)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
