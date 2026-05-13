# Proposal: Engine-Sim Lite for ESP32 & Mobile

## 1. Reality Check (Intel MBP Baseline)
*   **Test Environment:** 2017 MBP (Intel i7).
*   **Performance:** ~200% of budget (Double-speed playback or massive stuttering).
*   **Partial Success:** Only runs at `--sim-freq 8000` in threaded mode.
*   **Conclusion:** The ESP32 (240MHz) cannot run the code as-is. Even a "powerful" microcontroller will struggle without radical optimization.

## 2. Hardware "Oomph" Options
If the goal is to move beyond the ESP32 to hardware that can handle the load:

| Platform | Clock Speed | FPU | Price | Pros/Cons |
| :--- | :--- | :--- | :--- | :--- |
| **Teensy 4.1** | 600 MHz | High-end FPU | ~$40 | **Pros:** Robust audio library, straight C++ swap. **Cons:** Price. |
| **Pi Zero 2W** | 1.0 GHz (4-core) | NEON (SIMD) | ~$15 | **Pros:** Runs Linux/Headless, massive headroom. **Cons:** Slower boot, power draw. |
| **STM32H7** | 550 MHz | Robust FPU | ~$25 | **Pros:** Industrial standard. **Cons:** Steep learning curve. |

## 3. Mandatory Software Refactors
Regardless of hardware, these three changes are required for real-time performance on anything but a high-end PC:

*   **Refactor A: Double to Float (`real_t`)**
    *   Currently, `engine-sim` uses `double` for every calculation. 
    *   **Action:** Define `typedef float real_t` and replace all `double` instances. This will likely give a 2x-5x boost on microcontrollers with FPUs.
*   **Refactor B: Analytic Piston Geometry**
    *   Currently, `engine-sim` uses a Sequential Constraint Solver (SCS) to solve piston positions ($O(n)$ iterations).
    *   **Action:** Replace the solver with the analytic formula for the "Crank-Slider" mechanism. This makes piston positioning $O(1)$ and eliminates the heaviest part of the physics loop.
*   **Refactor C: Audio Upsampling**
    *   Run Physics at 11kHz or 22kHz and use Linear Interpolation to generate 44.1kHz audio. This reduces gas-law math by 2x-4x.

## 4. "Cheap" Exhaust Augmentation
Instead of 1D CFD, we adopt a "Heuristic Fluid" approach for realism:
*   **Unburnt Fuel Support:** Add `Mix` tracking to the exhaust runners.
*   **Overrun Pops:** If `Runner.p_fuel > threshold` AND `Runner.T > ignition_point`, trigger a single-frame energy burst.
*   **Rounded Flow:** Instead of 1-node runners, use a fixed 3-node chain. It approximates wave delay without the Euler equations.
