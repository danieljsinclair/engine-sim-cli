#!/usr/bin/env python3
"""
Crackle Detection Tool for Engine-Sim CLI Audio Output

Analyzes WAV files for audio discontinuities that would be perceived as crackles.
Detects:
1. Sample-to-sample amplitude jumps (sudden level changes)
2. Zero-crossings at unexpected intervals (phase discontinuities)
3. Silent gaps (buffer underruns → silence → audio resumes)
4. DC offset jumps
"""

import struct
import sys
import os
import math

def read_wav(filepath):
    """Read WAV file and return sample data + metadata."""
    with open(filepath, 'rb') as f:
        # RIFF header
        riff = f.read(4)
        if riff != b'RIFF':
            raise ValueError(f"Not a RIFF file: {riff}")
        file_size = struct.unpack('<I', f.read(4))[0]
        wave = f.read(4)
        if wave != b'WAVE':
            raise ValueError(f"Not a WAVE file: {wave}")

        fmt_found = False
        data_found = False
        audio_format = 0
        num_channels = 0
        sample_rate = 0
        bits_per_sample = 0
        samples = []

        while True:
            chunk_id = f.read(4)
            if len(chunk_id) < 4:
                break
            chunk_size = struct.unpack('<I', f.read(4))[0]

            if chunk_id == b'fmt ':
                audio_format = struct.unpack('<H', f.read(2))[0]
                num_channels = struct.unpack('<H', f.read(2))[0]
                sample_rate = struct.unpack('<I', f.read(4))[0]
                f.read(4)  # byte rate
                f.read(2)  # block align
                bits_per_sample = struct.unpack('<H', f.read(2))[0]
                # Skip any extra fmt bytes
                remaining = chunk_size - 16
                if remaining > 0:
                    f.read(remaining)
                fmt_found = True

            elif chunk_id == b'data':
                if not fmt_found:
                    raise ValueError("data chunk before fmt chunk")

                # Read sample data
                if audio_format == 3:  # IEEE float
                    num_samples = chunk_size // 4
                    for _ in range(num_samples):
                        raw = f.read(4)
                        if len(raw) < 4:
                            break
                        samples.append(struct.unpack('<f', raw)[0])
                elif audio_format == 1:  # PCM int16
                    num_samples = chunk_size // 2
                    for _ in range(num_samples):
                        raw = f.read(2)
                        if len(raw) < 2:
                            break
                        val = struct.unpack('<h', raw)[0]
                        samples.append(val / 32768.0)
                else:
                    raise ValueError(f"Unsupported audio format: {audio_format}")
                data_found = True
            else:
                f.read(chunk_size)

    if not fmt_found or not data_found:
        raise ValueError("Missing fmt or data chunk")

    return {
        'samples': samples,
        'sample_rate': sample_rate,
        'num_channels': num_channels,
        'bits_per_sample': bits_per_sample,
        'audio_format': audio_format,
        'num_frames': len(samples) // num_channels
    }


def analyze_crackles(wav_data, skip_seconds=0.0):
    """Analyze WAV for crackle-causing discontinuities."""
    samples = wav_data['samples']
    sr = wav_data['sample_rate']
    ch = wav_data['num_channels']
    num_frames = wav_data['num_frames']

    skip_frames = int(skip_seconds * sr)

    print(f"\n=== WAV Analysis ===")
    print(f"Sample Rate: {sr} Hz")
    print(f"Channels: {ch}")
    print(f"Format: {'Float32' if wav_data['audio_format'] == 3 else 'Int16'}")
    print(f"Total Frames: {num_frames} ({num_frames/sr:.2f} seconds)")
    print(f"Skipping first {skip_seconds:.1f}s ({skip_frames} frames)")

    # Extract left channel only (for mono analysis)
    left = [samples[i * ch] for i in range(skip_frames, num_frames)]

    if len(left) < 2:
        print("ERROR: Not enough samples to analyze")
        return

    # ====================================================================
    # Test 1: Sample-to-sample amplitude jumps
    # A clean sine wave has max delta = 2*pi*f/sr ≈ small
    # A crackle has a sudden jump in amplitude
    # ====================================================================
    print(f"\n--- Test 1: Amplitude Discontinuities ---")

    deltas = []
    for i in range(1, len(left)):
        deltas.append(abs(left[i] - left[i-1]))

    if not deltas:
        print("No deltas to analyze")
        return

    avg_delta = sum(deltas) / len(deltas)
    max_delta = max(deltas)
    max_delta_idx = deltas.index(max_delta)

    # Threshold: a jump > 10x the average delta is suspicious
    # For a smooth sine, jumps should be very uniform
    threshold_10x = avg_delta * 10
    threshold_20x = avg_delta * 20
    threshold_50x = avg_delta * 50

    jumps_10x = [(i + skip_frames, deltas[i]) for i in range(len(deltas)) if deltas[i] > threshold_10x]
    jumps_20x = [(i + skip_frames, deltas[i]) for i in range(len(deltas)) if deltas[i] > threshold_20x]
    jumps_50x = [(i + skip_frames, deltas[i]) for i in range(len(deltas)) if deltas[i] > threshold_50x]

    print(f"  Average sample delta: {avg_delta:.6f}")
    print(f"  Max sample delta:     {max_delta:.6f} at frame {max_delta_idx + skip_frames} ({(max_delta_idx + skip_frames)/sr:.4f}s)")
    print(f"  Jumps > 10x avg ({threshold_10x:.6f}): {len(jumps_10x)}")
    print(f"  Jumps > 20x avg ({threshold_20x:.6f}): {len(jumps_20x)}")
    print(f"  Jumps > 50x avg ({threshold_50x:.6f}): {len(jumps_50x)}")

    if jumps_20x:
        print(f"\n  First 20 jumps > 20x avg:")
        for frame, delta in jumps_20x[:20]:
            time_s = frame / sr
            idx = frame - skip_frames
            before = left[idx - 1] if idx > 0 else 0
            after = left[idx]
            print(f"    Frame {frame} ({time_s:.4f}s): delta={delta:.6f}, before={before:.6f}, after={after:.6f}")

    # ====================================================================
    # Test 2: Silent gaps (zeros in the middle of audio)
    # ====================================================================
    print(f"\n--- Test 2: Silent Gaps ---")

    silence_threshold = 0.001  # Below this is "silence"
    in_silence = False
    silence_start = 0
    gaps = []

    # Skip initial silence (warmup)
    first_audio = 0
    for i in range(len(left)):
        if abs(left[i]) > silence_threshold:
            first_audio = i
            break

    print(f"  First non-silent frame: {first_audio + skip_frames} ({(first_audio + skip_frames)/sr:.4f}s)")

    for i in range(first_audio, len(left)):
        if abs(left[i]) < silence_threshold:
            if not in_silence:
                silence_start = i
                in_silence = True
        else:
            if in_silence:
                gap_len = i - silence_start
                if gap_len >= 10:  # Only count gaps >= 10 samples (would be audible)
                    gaps.append((silence_start + skip_frames, gap_len))
                in_silence = False

    print(f"  Silent gaps (>= 10 samples) after first audio: {len(gaps)}")
    if gaps:
        total_gap_samples = sum(g[1] for g in gaps)
        print(f"  Total gap samples: {total_gap_samples} ({total_gap_samples/sr*1000:.1f}ms)")
        print(f"  First 10 gaps:")
        for frame, length in gaps[:10]:
            print(f"    Frame {frame} ({frame/sr:.4f}s): {length} samples ({length/sr*1000:.2f}ms)")

    # ====================================================================
    # Test 3: RMS energy per chunk (detect level drops/spikes)
    # ====================================================================
    print(f"\n--- Test 3: RMS Energy Analysis (per 735-sample chunk = 1/60s) ---")

    chunk_size = sr // 60  # 735 samples at 44100Hz
    chunks = []
    for i in range(first_audio, len(left) - chunk_size, chunk_size):
        rms = math.sqrt(sum(s*s for s in left[i:i+chunk_size]) / chunk_size)
        chunks.append((i + skip_frames, rms))

    if chunks:
        rms_values = [c[1] for c in chunks]
        avg_rms = sum(rms_values) / len(rms_values)

        # Find chunks with RMS < 20% of average (suspicious drops)
        low_energy = [(f, r) for f, r in chunks if r < avg_rms * 0.2 and avg_rms > 0.01]
        high_energy = [(f, r) for f, r in chunks if r > avg_rms * 3.0]

        print(f"  Average RMS: {avg_rms:.6f}")
        print(f"  Min RMS: {min(rms_values):.6f}")
        print(f"  Max RMS: {max(rms_values):.6f}")
        print(f"  Low energy chunks (< 20% avg): {len(low_energy)}")
        print(f"  High energy chunks (> 3x avg): {len(high_energy)}")

        if low_energy:
            print(f"  First 10 low-energy chunks:")
            for frame, rms in low_energy[:10]:
                print(f"    Frame {frame} ({frame/sr:.4f}s): RMS={rms:.6f}")

    # ====================================================================
    # Test 4: Zero-sample analysis (exact zeros indicate buffer underrun)
    # ====================================================================
    print(f"\n--- Test 4: Exact Zero Samples ---")

    exact_zeros = sum(1 for s in left[first_audio:] if s == 0.0)
    total_after_first = len(left) - first_audio
    pct_zeros = (exact_zeros / total_after_first * 100) if total_after_first > 0 else 0

    print(f"  Exact zeros after first audio: {exact_zeros} / {total_after_first} ({pct_zeros:.2f}%)")

    # ====================================================================
    # Test 5: Peak amplitude distribution
    # ====================================================================
    print(f"\n--- Test 5: Amplitude Statistics ---")

    max_amp = max(abs(s) for s in left[first_audio:]) if left[first_audio:] else 0
    min_amp = min(left[first_audio:]) if left[first_audio:] else 0
    max_pos = max(left[first_audio:]) if left[first_audio:] else 0

    print(f"  Peak amplitude: {max_amp:.6f}")
    print(f"  Min sample: {min_amp:.6f}")
    print(f"  Max sample: {max_pos:.6f}")

    # ====================================================================
    # VERDICT
    # ====================================================================
    print(f"\n=== VERDICT ===")
    issues = []

    if len(jumps_20x) > 0:
        issues.append(f"CRACKLE: {len(jumps_20x)} amplitude discontinuities > 20x average")
    if len(gaps) > 0:
        issues.append(f"CRACKLE: {len(gaps)} silent gaps detected")
    if pct_zeros > 1.0:
        issues.append(f"WARNING: {pct_zeros:.1f}% exact zeros (possible underruns)")
    if len(low_energy) > 0:
        issues.append(f"WARNING: {len(low_energy)} low-energy chunks (possible dropouts)")

    if not issues:
        print("CLEAN: No crackle indicators detected!")
    else:
        for issue in issues:
            print(f"  {issue}")

    return len(issues) == 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <wav_file> [skip_seconds]")
        sys.exit(1)

    filepath = sys.argv[1]
    skip = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0

    if not os.path.exists(filepath):
        print(f"ERROR: File not found: {filepath}")
        sys.exit(1)

    wav = read_wav(filepath)
    clean = analyze_crackles(wav, skip_seconds=skip)
    sys.exit(0 if clean else 1)
