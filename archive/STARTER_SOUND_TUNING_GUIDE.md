# Starter Motor Sound Tuning Guide

## Overview

The improved `generate_starter_sound.py` now generates a realistic DC starter motor sound based on research from actual automotive starter motors. The key insight is that **real motors are noise-dominated**, not clean tone generators.

## What Changed

### Before (Synthetic/Artificial)
- Fundamental frequency: 280 Hz
- Clean harmonic structure: 1x, 4x, 8x, 12x, 16x multiples (like a synthesizer)
- Inharmonic content: minimal
- Noise amount: 15% (background effect)
- Problem: Sounded like "a twizzler that flies away" - too clean and artificial

### After (Research-Based)
- Fundamental frequency: 160 Hz (lower)
- Irregular harmonic spacing: 1.0x, 1.5x, 2.8x, 4.2x, 6.0x, 8.5x (natural irregularity)
- Added phase jitter to simulate brush switching instability
- Noise amount: 42% (now DOMINANT - primary character)
- Added research-backed frequency components:
  - 84 Hz: Unbalanced rotor bearing rumble (from PMC research)
  - 252 Hz: Housing/ventilation hole resonance (from PMC research)
  - 508 Hz: Brush commutation noise (from PMC research)

## How to Tune Further

### 1. Fundamental Frequency (Controls Overall Pitch)
```python
FUNDAMENTAL_FREQ = 160.0  # Range: 100-250 Hz
```
- **Lower (100-150 Hz)**: Deeper, more rumbling character
- **Higher (200-250 Hz)**: Thinner, more high-pitched whine
- Real starter motors: typically 150-200 Hz range

### 2. Inharmonic Ripple Structure (Controls "Tone Color")
```python
COMMUTATOR_HARMONICS = [1.0, 1.5, 2.8, 4.2, 6.0, 8.5]
COMMUTATOR_AMPS = [0.25, 0.12, 0.15, 0.08, 0.05, 0.03]
```
- The irregular spacing (1.5x, 2.8x instead of clean 2x, 3x) creates natural character
- To make it MORE synthetic: change to [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
- To make it MORE organic: add more irregular values like [1.0, 1.3, 2.1, 3.7, 5.5, 8.2]
- Reduce amplitude values to make harmonics more masked by noise

### 3. Mechanical Components (Research-Backed)
These are based on real motor acoustic measurements:

#### Bearing Rumble (84 Hz)
```python
BEARING_RUMBLE_FREQ = 84.0  # Hz - unbalanced rotor rotation
BEARING_RUMBLE_AMP = 0.25   # Range: 0.15-0.35
```
- Increase for older, worn motors
- Decrease for smooth, balanced motors

#### Ventilation/Housing (252 Hz)
```python
VENTILATION_FREQ = 252.0  # Hz - motor case resonance
VENTILATION_AMP = 0.15    # Range: 0.08-0.25
```
- This is a fixed resonance of the motor housing
- Increase to emphasize motor case ringing
- Decrease if too "tinny"

#### Brush Commutation (508 Hz)
```python
BRUSH_NOISE_FREQ = 508.0   # Hz - brush switching (speed-independent)
BRUSH_NOISE_AMP = 0.08     # Range: 0.04-0.15
```
- Increase for worn brushes or high electrical noise
- Decrease for smooth brushes
- Note: This frequency stays constant regardless of RPM (unlike other components)

### 4. Broadband Noise (The Most Important)
```python
NOISE_AMOUNT = 0.35  # Range: 0.20-0.50
```
- **0.20 (Low)**: More tonal, cleaner character
- **0.35 (Current)**: Good balance - realistic motor
- **0.50 (High)**: Very rough, grinding character (worn motor)

This is the PRIMARY component that makes it sound real. Real motors are ~40-50% noise.

### 5. Load Variation (Compression Stroke Simulation)
```python
LOAD_VARIATION_FREQ = 6.0    # Hz (4-6 Hz typical for 4-cyl at idle)
LOAD_VARIATION_DEPTH = 0.25  # Range: 0.15-0.40
```
- **Lower (0.15)**: Smooth spin-up, less struggling
- **Higher (0.40)**: Very choppy, more engine resistance
- Adjust LOAD_VARIATION_FREQ to match your engine:
  - 4-cylinder: 6-8 Hz
  - 6-cylinder: 10-12 Hz
  - 8-cylinder: 13-16 Hz

### 6. Amplitude Mix (Signal Balance)
```python
signal = (
    0.25 * commutator_signal +  # Reduce this for less tone
    0.15 * bearing_rumble +      # Low frequency growl
    0.10 * ventilation +         # Housing resonance
    0.08 * brush_noise +         # Brush switching sparkle
    0.42 * noise                 # Noise DOMINATES
)
```

The sum should equal ~1.0 (0.90 here before normalization). To emphasize different characteristics:
- More tone: increase `commutator_signal` to 0.35-0.40, decrease `noise` to 0.30
- More growl: increase `bearing_rumble` to 0.25, reduce others slightly
- More grinding: increase `noise` to 0.50-0.55, reduce `commutator_signal` to 0.15

### 7. Phase Jitter (Simulates Brush Irregularity)
```python
if i % 480 == 0:  # Change phase drift every 10ms
    phase_jitter_state = np.random.uniform(-0.3, 0.3)
```
- Increase `-0.3, 0.3` range to `-0.5, 0.5` for more chaotic character
- Decrease to `-0.1, 0.1` for smoother operation
- This simulates the irregular nature of brush commutation

## Testing Procedure

1. Run the generator:
```bash
python3 generate_starter_sound.py
```

2. Listen for these characteristics:
   - **0.0-0.5s**: Solenoid click + engagement kick
   - **0.5-3.0s**: Struggling spin-up (pitch increases as load drops)
   - **3.0+s**: Steady cranking tone with load variations

3. Compare to your target:
   - Does it sound like an electric motor or a pure tone?
   - Can you hear the background growling/grinding?
   - Is the pitch variation natural?

## Reference Values from Research

These are based on DC motor acoustic analysis (PMC, ScienceDirect research):

| Component | Frequency | Source |
|-----------|-----------|--------|
| Unbalanced rotor | 84 Hz | Rotor imbalance, bearing forces |
| Fundamental motor tone | 150-200 Hz | Motor speed (proportional to RPM) |
| Housing resonance | 250-280 Hz | Ventilation holes, case structure |
| Brush switching | 500-520 Hz | Brush-commutator contact (speed-independent) |
| Broadband noise | 20-8000 Hz | Bearing friction, brush arcing, air turbulence |

## Frequency Sweep Behavior

The fundamental frequency is modulated by:
- **RPM**: From 20 RPM (engagement) to 200 RPM (cranking)
- **Load variation**: 6 Hz compression pulses create pitch wobble
- **Envelope**: Soft fade-in over 200ms

The non-RPM components (84 Hz, 252 Hz, 508 Hz) remain fixed, which is physically accurate.

## Common Tuning Scenarios

### Make it sound like a worn starter:
```python
BEARING_RUMBLE_AMP = 0.35  # Increase bearing rumble
BRUSH_NOISE_AMP = 0.15     # Increase brush noise
NOISE_AMOUNT = 0.45        # More grinding
```

### Make it sound smoother/higher-end motor:
```python
BEARING_RUMBLE_AMP = 0.15  # Reduce bearing rumble
BRUSH_NOISE_AMP = 0.04     # Reduce brush noise
NOISE_AMOUNT = 0.25        # Less broadband noise
FUNDAMENTAL_FREQ = 200.0   # Slightly higher pitch
```

### Make it sound like it's struggling more:
```python
LOAD_VARIATION_DEPTH = 0.40  # More dramatic pitch dips
LOAD_VARIATION_FREQ = 8.0    # Faster pulses
NOISE_AMOUNT = 0.48          # More grinding
RAMPUP_TIME = 3.5            # Slower acceleration
```

## Files Modified

- `/Users/danielsinclair/vscode/engine-sim-cli/generate_starter_sound.py` - Main generator script

## Resources Used

- [DC Motor Noise Analysis Research](https://pmc.ncbi.nlm.nih.gov/articles/PMC6187501/)
- [Spectrogram Analysis for Motor Speeds](https://www.sciencedirect.com/science/article/pii/S2405896318309133)
- [Engine Sound Analysis](https://acta-acustica.edpsciences.org/articles/aacus/full_html/2023/01/aacus220112/aacus220112.html)

## Key Principle

**Real motors are fundamentally noise-dominated, not tone-dominated.** The harmonics should be barely perceptible under the broadband mechanical noise. If you can clearly hear individual sine waves, it's too synthetic. If you mostly hear grinding/rushing air, it's too harsh. The sweet spot is when the two blend seamlessly into a cohesive motor sound.
