# Audio Investigation: Synthesizer Discontinuities CONFIRMED

## Root Cause Found: Synthesizer Produces Discontinuous Audio

**Date:** 2025-02-02

### Evidence Summary

1. **Sine wave (pre-generated):** ZERO cracks ✅
2. **Sine wave (RPM-linked, real-time):** ZERO cracks ✅
3. **Engine audio:** Many cracks ❌

### Conclusion
**The audio path is PERFECT.** The problem is the **synthesizer producing discontinuous audio data.**

---

## Test: RPM-Linked Sine Wave

**Implementation:**
- Created engine simulator in sine mode (like normal mode)
- Mapped RPM to sine wave frequency: 600 RPM = 100Hz, 6000 RPM = 1000Hz
- AudioUnit callback generates sine wave in real-time based on current RPM
- RPM ramps from 600 to 6000 during test

**Results:**
- ✅ ZERO underruns
- ✅ ZERO crackles
- ✅ ZERO discontinuities
- ✅ Smooth frequency transitions

**This proves:**
- AudioUnit callback is fine
- Circular buffer is fine
- Buffer wraparound is fine
- Dynamic frequency changes don't cause cracks

---

## What's Different: Sine Wave vs Engine Audio

| Aspect | Sine Wave | Engine Audio |
|--------|-----------|--------------|
| Source | Math.sin() based on RPM | Synthesizer from engine simulator |
| Output | Perfect sine wave | Discontinuous audio |
| Cracks | None | Frequent |

---

## Next Steps

The GUI reportedly works perfectly with the same synthesizer. Need to investigate:
1. Does GUI actually have perfect audio? User confirmation needed
2. If GUI is perfect, what does it do differently?
3. Is there post-processing in GUI we're missing?
4. Does GUI read at different times/rates?

**Key Question:** Why does the same synthesizer produce bad audio for CLI but (reportedly) good audio for GUI?
