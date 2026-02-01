# TA2 Physics and Audio Flow Investigation Report

**Mission**: Trace complete data flow from engine physics to audio output to identify root cause of audio dropouts at 15%+ throttle in CLI vs perfect audio in GUI.

**Investigation Date**: 2026-01-29
**Investigator**: TA2 (Technical Architect 2)

---

## Executive Summary

### Top 5 Critical Findings

1. **AUDIO THREAD IS RUNNING IN CLI** - The CLI correctly starts the audio rendering thread at line 702 of `engine_sim_cli.cpp`, matching the GUI pattern exactly. This is NOT the root cause.

2. **GUI USES `setSpeedControl()` - CLI USES `setThrottle()`** - CRITICAL DIFFERENCE:
   - GUI (line 800): `m_iceEngine->setSpeedControl(m_speedSetting)` → Uses Governor with closed-loop feedback
   - CLI (line 797, 941): `EngineSimSetThrottle(handle, throttle)` → Bypasses Governor, uses direct throttle
   - Governor provides safety feature: **Full throttle (1.0) at low RPM** (governor.cpp:43-46)

3. **AUDIO SYNTHESIS IS THROTTLE-DEPENDENT** - Exhaust flow calculation (piston_engine_simulator.cpp:394-398) shows:
   - `exhaustFlow = attenuation_3 * 1600 * (chamber->m_exhaustRunnerAndPrimary.pressure() - 1.0 atm)`
   - At 15% throttle, manifold pressure is low → low exhaust flow → minimal audio generation

4. **SYNTHESIZER INPUT RATE MATCHES SIMULATION RATE** - `writeInput()` is called ONCE per simulation step (simulator.cpp:152), which runs at simulation frequency (10,000 Hz default by default, 8,000 Hz in GUI).

5. **CLI READS FROM AUDIO BUFFER CORRECTLY** - CLI uses `EngineSimReadAudioBuffer` (line 965) which reads from the audio thread's output buffer, matching GUI pattern (engine_sim_application.cpp:274).

---

## Detailed Evidence

### 1. Complete Engine Simulation Flow

#### GUI Flow (Working Perfectly)
```
User Input (Q/W/E/R keys)
  ↓
processEngineInput() [engine_sim_application.cpp:670]
  ↓
m_targetSpeedSetting = 0.01, 0.1, 0.2, or 1.0 [lines 778-789]
  ↓
m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting [line 798] (smoothed)
  ↓
m_iceEngine->setSpeedControl(m_speedSetting) [line 800] ← CRITICAL: Uses Governor
  ↓
Governor::update() [governor.cpp:36-52]
  ├── m_targetSpeed = (1 - s) * m_minSpeed + s * m_maxSpeed [line 33]
  ├── Calculates error: ds = m_targetSpeed² - currentSpeed² [line 38]
  ├── Updates velocity: m_velocity += (-ds * m_k_s - m_velocity * m_k_d) * dt [line 40]
  └── SAFETY FEATURE [lines 43-46]:
      if (abs(currentSpeed) < 0.5 * m_minSpeed) {
          m_velocity = 0;
          m_currentThrottle = 1.0;  ← FULL THROTTLE AT LOW RPM!
      }
  ↓
engine->setThrottle(1 - pow(1 - m_currentThrottle, m_gamma)) [line 51]
  ↓
process() [engine_sim_application.cpp:202]
  ↓
m_simulator->startFrame(1 / avgFramerate) [line 235]
  ↓
while (m_simulator->simulateStep()) [line 239] ← Multiple physics steps per frame
  ├── simulateStep_() [piston_engine_simulator.cpp:282-320]
  │   ├── Ignition module update
  │   ├── Chamber updates (combustion physics)
  │   └── Fluid simulation (8 substeps per step) [lines 304-317]
  ├── writeToSynthesizer() [piston_engine_simulator.cpp:371-413]
  │   └── synthesizer().writeInput(m_exhaustFlowStagingBuffer) [line 412]
  └── m_currentIteration++
  ↓
m_simulator->endFrame() [line 245]
  ├── m_synthesizer.endInputBlock() [simulator.cpp:167]
  └── NOTIFIES AUDIO THREAD to process input [synthesizer.cpp:212]
  ↓
readAudioOutput() [engine_sim_application.cpp:274]
  └── Reads from audio buffer filled by audio thread
```

#### CLI/Bridge Flow (Dropouts at 15%+ Throttle)
```
User Input (command line --load or interactive)
  ↓
EngineSimSetThrottle(handle, throttle) [engine_sim_cli.cpp:797, 941]
  ↓
engine_sim_bridge.cpp:433
  ctx->engine->setSpeedControl(position)  ← Calls Governor correctly!
  ↓
Engine::setSpeedControl(s) [engine.cpp:138-139]
  m_throttle->setSpeedControl(s)
  ↓
Governor::setSpeedControl(s) [governor.cpp:30-34]
  m_targetSpeed = (1 - s) * m_minSpeed + s * m_maxSpeed
  ↓
EngineSimUpdate(handle, updateInterval) [engine_sim_cli.cpp:798, 942]
  ↓
engine_sim_bridge.cpp:459
  ctx->simulator->startFrame(deltaTime)
  ↓
while (ctx->simulator->simulateStep()) [engine_sim_bridge.cpp:461]
  ├── simulateStep_() [piston_engine_simulator.cpp:282-320]
  ├── writeToSynthesizer() [piston_engine_simulator.cpp:371-413]
  └── ... SAME AS GUI ...
  ↓
ctx->simulator->endFrame() [engine_sim_bridge.cpp:465]
  ↓
EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten) [line 965]
  ↓
engine_sim_bridge.cpp:608
  ctx->simulator->readAudioOutput(frames, ctx->audioConversionBuffer)
  ↓
synthesizer().readAudioOutput(samples, target) [simulator.cpp:163]
```

**CRITICAL OBSERVATION**: The CLI flow is IDENTICAL to the GUI flow in terms of:
- Audio thread usage (both use async rendering)
- Governor usage (both call `setSpeedControl`)
- Simulation steps per frame
- Audio buffer reading

**However**, there's a subtle difference in HOW the throttle value is determined...

---

### 2. Audio Synthesis Process (synthesizer.cpp)

#### `renderAudio()` Function Analysis [Lines 222-256]

**Purpose**: Generate audio samples from input exhaust flow data.

**Inputs Required**:
- `m_inputChannels[i].data` - Ring buffer containing exhaust flow samples
- `m_audioParameters` - Audio processing parameters (volume, convolution, noise, etc.)
- `m_audioBuffer` - Output ring buffer for rendered audio

**How Many Samples Can It Generate Per Call?**
```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),  // Max 2000 samples in output buffer
    (int)m_inputChannels[0].data.size()              // Limited by available input
); [lines 232-234]
```

**Answer**: Up to 2000 samples per call, but limited by:
1. Available space in output audio buffer (max 2000)
2. Available input samples in input buffer

**CRITICAL: At 15% throttle, is renderAudio() being called enough times?**

**Evidence from Code**:
- `renderAudio()` is called by the audio thread in a loop [lines 215-219]:
  ```cpp
  void Synthesizer::audioRenderingThread() {
      while (m_run) {
          renderAudio();
      }
  }
  ```
- The audio thread waits on condition variable [lines 225-230]:
  ```cpp
  m_cv0.wait(lk0, [this] {
      const bool inputAvailable =
          m_inputChannels[0].data.size() > 0
          && m_audioBuffer.size() < 2000;
      return !m_run || (inputAvailable && !m_processed);
  });
  ```

**What prevents it from generating enough audio?**

1. **Input buffer starvation**: If `m_inputChannels[0].data.size() == 0`, the thread blocks
2. **Output buffer full**: If `m_audioBuffer.size() >= 2000`, the thread blocks
3. **Already processed flag**: If `m_processed == true`, the thread blocks until `endInputBlock()` is called

**Root Cause Analysis**:
- `writeInput()` [lines 168-195] is called ONCE per simulation step (simulator.cpp:152)
- Each `writeInput()` call writes approximately `m_audioSampleRate / m_inputSampleRate` samples
- At 48kHz audio rate and 10kHz simulation rate: ~4.8 input samples per simulation step
- At 60 FPS: 166 simulation steps per frame = 166 * 4.8 = ~800 input samples per frame
- **This should be sufficient** if the input samples have sufficient magnitude

**THE REAL PROBLEM**: At 15% throttle, exhaust flow magnitude is TOO LOW, causing:
1. Minimal audio signal generation
2. Perceived dropouts (silence) even though the system is working correctly

---

### 3. Input Data Flow - How Exhaust Flow Gets to Synthesizer

#### Function Chain:
```
simulateStep() [simulator.cpp:98-156]
  ↓
simulateStep_() [piston_engine_simulator.cpp:282-320]
  ├── Updates ignition [lines 284-291]
  ├── Updates chambers [lines 293-294]
  ├── Fluid simulation (8 substeps) [lines 304-317]
  └── Calculates exhaust flow for each cylinder
  ↓
writeToSynthesizer() [piston_engine_simulator.cpp:371-413]
  ├── For each cylinder [lines 383-410]:
  │   ├── Get exhaust pressure: chamber->m_exhaustRunnerAndPrimary.pressure()
  │   ├── Calculate exhaust flow [lines 394-398]:
  │   │   exhaustFlow = attenuation_3 * 1600 * (
  │   │       1.0 * (pressure - 1.0 atm)
  │   │       + 0.1 * dynamicPressure(1.0, 0.0)
  │   │       + 0.1 * dynamicPressure(-1.0, 0.0)
  │   │   )
  │   ├── Apply delay filter [line 402-403]:
  │   │   delayedExhaustPulse = m_delayFilters[i].fast_f(exhaustFlow)
  │   └── Accumulate to staging buffer [lines 406-409]
  └── synthesizer().writeInput(m_exhaustFlowStagingBuffer) [line 412]
  ↓
Synthesizer::writeInput(const double *data) [synthesizer.cpp:168-195]
  ├── Calculates write offset [line 169]:
  │   m_inputWriteOffset += (double)m_audioSampleRate / m_inputSampleRate
  ├── For each channel [lines 174-192]:
  │   ├── Interpolates between samples
  │   ├── Applies antialiasing filter [line 188]
  │   └── Writes to ring buffer: buffer.write(sample)
  └── Updates last input sample offset [line 194]
```

#### How Often Is It Called?
**ONCE per simulation step** - simulator.cpp:152 calls `writeToSynthesizer()` at the end of every `simulateStep()`.

#### What Controls the Rate?
The simulation frequency:
- GUI: Uses engine's default (typically 8000 Hz based on engine_sim_application.cpp:169 initialization)
- CLI: Configurable, defaults to 10000 Hz (engine_sim_cli.cpp:594)

**CRITICAL**: The CLI's 10000 Hz simulation frequency vs GUI's 8000 Hz means:
- CLI: 10,000 simulation steps per second
- GUI: 8,000 simulation steps per second
- This should give CLI MORE input samples, not fewer!

---

### 4. Audio Thread Coordination

#### GUI: How It Coordinates With Main Thread

**Audio Thread Startup** [engine_sim_application.cpp:509]:
```cpp
m_simulator->startAudioRenderingThread();
```

**Main Thread Flow** [engine_sim_application.cpp:235-245]:
```cpp
m_simulator->startFrame(1 / avgFramerate);
while (m_simulator->simulateStep()) {
    // Physics + writeToSynthesizer()
}
m_simulator->endFrame();  // Calls m_synthesizer.endInputBlock()
```

**endInputBlock()** [synthesizer.cpp:197-213]:
```cpp
void Synthesizer::endInputBlock() {
    std::unique_lock<std::mutex> lk(m_inputLock);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.removeBeginning(m_inputSamplesRead);
    }

    m_latency = m_inputChannels[0].data.size();
    m_inputSamplesRead = 0;
    m_processed = false;  // ← CRITICAL: Resets flag

    lk.unlock();
    m_cv0.notify_one();  // ← CRITICAL: Wakes audio thread
}
```

**Audio Thread** [synthesizer.cpp:215-256]:
```cpp
void Synthesizer::audioRenderingThread() {
    while (m_run) {
        renderAudio();  // Waits for m_processed == false
    }
}
```

**Race Condition Prevention**:
- Mutex `m_lock0` protects audio buffer and processed flag
- Condition variable `m_cv0` coordinates between threads
- `m_processed` flag ensures renderAudio() is called only once per input block

#### CLI: Are We Using the Audio Thread?

**Evidence from engine_sim_cli.cpp:698-708**:
```cpp
// CRITICAL: Start the audio rendering thread (matching GUI pattern)
result = EngineSimStartAudioThread(handle);
if (result != ESIM_SUCCESS) {
    std::cerr << "ERROR: Failed to start audio thread: " << EngineSimGetLastError(handle) << "\n";
    EngineSimDestroy(handle);
    return 1;
}
std::cout << "[3/5] Audio rendering thread started (asynchronous mode)\n";
```

**Bridge Implementation** [engine_sim_bridge.cpp:379-396]:
```cpp
EngineSimResult EngineSimStartAudioThread(EngineSimHandle handle) {
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    EngineSimContext* ctx = getContext(handle);

    if (!ctx->engine) {
        ctx->setError("No engine loaded. Call EngineSimLoadScript first.");
        return ESIM_ERROR_NOT_INITIALIZED;
    }

    ctx->simulator->startAudioRenderingThread();

    return ESIM_SUCCESS;
}
```

**Answer**: **YES, CLI IS USING THE AUDIO THREAD** - exactly like the GUI!

#### Are We Missing Coordination in CLI?

**CLI Main Loop** [engine_sim_cli.cpp:941-965]:
```cpp
EngineSimUpdate(handle, updateInterval);  // Runs physics + calls endInputBlock()
// ...
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**EngineSimUpdate** [engine_sim_bridge.cpp:459-465]:
```cpp
ctx->simulator->startFrame(deltaTime);
while (ctx->simulator->simulateStep()) {
    // Process all simulation steps for this frame
}
ctx->simulator->endFrame();  // ← Calls endInputBlock() and notifies audio thread
```

**Answer**: **NO, we're NOT missing coordination** - CLI follows the exact same pattern as GUI!

---

### 5. Throttle vs Speed Control - THE CRITICAL DIFFERENCE

#### GUI: Uses `setSpeedControl()` → Governor

**Code** [engine_sim_application.cpp:776-800]:
```cpp
const double prevTargetThrottle = m_targetSpeedSetting;
m_targetSpeedSetting = fineControlMode ? m_targetSpeedSetting : 0.0;
if (m_engine.IsKeyDown(ysKey::Code::Q)) {
    m_targetSpeedSetting = 0.01;  // 1% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::W)) {
    m_targetSpeedSetting = 0.1;   // 10% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::E)) {
    m_targetSpeedSetting = 0.2;   // 20% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::R)) {
    m_targetSpeedSetting = 1.0;   // 100% speed
}
// ...

m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting;  // Smooth transition

m_iceEngine->setSpeedControl(m_speedSetting);  // ← CRITICAL: Uses Governor!
```

**What Governor Does** [governor.cpp:36-52]:
```cpp
void Governor::update(double dt, Engine *engine) {
    const double currentSpeed = engine->getSpeed();
    const double ds = m_targetSpeed * m_targetSpeed - currentSpeed * currentSpeed;

    m_velocity += (dt * -ds * m_k_s - m_velocity * dt * m_k_d);
    m_velocity = clamp(m_velocity, m_minVelocity, m_maxVelocity);

    // ← CRITICAL SAFETY FEATURE:
    if (std::abs(currentSpeed) < std::abs(0.5 * m_minSpeed)) {
        m_velocity = 0;
        m_currentThrottle = 1.0;  // ← FULL THROTTLE at low RPM!
    }

    m_currentThrottle += m_velocity * dt;
    m_currentThrottle = clamp(m_currentThrottle);

    engine->setThrottle(1 - std::pow(1 - m_currentThrottle, m_gamma));
}
```

**Key Point**: Governor provides **closed-loop feedback** that automatically increases throttle when the engine is running slowly (during cranking, low load, etc.).

#### CLI: Uses `setThrottle()` Directly

**Code** [engine_sim_cli.cpp:797]:
```cpp
EngineSimSetThrottle(handle, warmupThrottle);
```

**Bridge Implementation** [engine_sim_bridge.cpp:415-437]:
```cpp
EngineSimResult EngineSimSetThrottle(
    EngineSimHandle handle,
    double position)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    if (position < 0.0 || position > 1.0) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    EngineSimContext* ctx = getContext(handle);
    ctx->throttlePosition.store(position, std::memory_order_relaxed);

    // Use the Governor abstraction for proper closed-loop feedback
    // This ensures the Governor's safety features (full throttle at low RPM) are active
    if (ctx->engine) {
        ctx->engine->setSpeedControl(position);  // ← Actually calls setSpeedControl!
    }

    return ESIM_SUCCESS;
}
```

**WAIT - CLI ALSO CALLS setSpeedControl()!**

Let me re-examine...

**Engine::setSpeedControl()** [engine.cpp:138-139]:
```cpp
void Engine::setSpeedControl(double s) {
    m_throttle->setSpeedControl(s);
}
```

**Throttle::setSpeedControl()** [throttle.cpp:11-13]:
```cpp
void Throttle::setSpeedControl(double s) {
    m_speedControl = s;
}
```

**Throttle::update()** [throttle.cpp:15-17]:
```cpp
void Throttle::update(double dt, Engine *engine) {
    /* void */  ← Does NOTHING!
}
```

**Governor::update()** [governor.cpp:36-52]:
```cpp
void Governor::update(double dt, Engine *engine) {
    // ... calculates and applies throttle ...
    engine->setThrottle(1 - std::pow(1 - m_currentThrottle, m_gamma));
}
```

**Question**: Is the CLI engine using a Governor or a basic Throttle?

**Need to check engine initialization...**

---

### 6. Simulation Timing

#### GUI: How Often Does Simulation Step Run?

**Frame Processing** [engine_sim_application.cpp:235]:
```cpp
m_simulator->startFrame(1 / avgFramerate);  // Typically 1/60 sec
```

**startFrame()** [simulator.cpp:67-96]:
```cpp
void Simulator::startFrame(double dt) {
    // ...
    m_synthesizer.setInputSampleRate(m_simulationFrequency * m_simulationSpeed);

    const double timestep = getTimestep();
    m_steps = (int)std::round((dt * m_simulationSpeed) / timestep);

    // Dynamic adjustment based on latency
    const double targetLatency = getSynthesizerInputLatencyTarget();
    if (m_synthesizer.getLatency() < targetLatency) {
        m_steps = static_cast<int>((m_steps + 1) * 1.1);
    }
    else if (m_synthesizer.getLatency() > targetLatency) {
        m_steps = static_cast<int>((m_steps - 1) * 0.9);
        if (m_steps < 0) {
            m_steps = 0;
        }
    }
    // ...
}
```

**Calculation**:
- `dt = 1/60` sec (at 60 FPS)
- `timestep = 1/8000` sec (at 8000 Hz simulation frequency)
- `m_steps = round((1/60) / (1/8000)) = round(133.33) = 133` steps per frame

**Result**: GUI runs ~133 simulation steps per frame at 60 FPS.

#### GUI: How Often Does Audio Render?

**Audio Thread Runs Continuously** - Independent of frame rate
- Waits for input data to be available
- Renders up to 2000 samples per call
- Runs as fast as possible

**Main Thread Reads Audio** [engine_sim_application.cpp:274]:
```cpp
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
```

This reads from the audio buffer that the audio thread is continuously filling.

#### CLI: Same Ratios?

**CLI Configuration** [engine_sim_cli.cpp:591-599]:
```cpp
EngineSimConfig config = {};
config.sampleRate = 48000;
config.inputBufferSize = config.sampleRate;  // 48000
config.audioBufferSize = config.sampleRate;  // 48000
config.simulationFrequency = 10000;  // ← Higher than GUI!
config.fluidSimulationSteps = 8;
config.targetSynthesizerLatency = 0.05;
```

**Calculation**:
- `dt = 1/60` sec
- `timestep = 1/10000` sec
- `m_steps = round((1/60) / (1/10000)) = round(166.67) = 167` steps per frame

**Result**: CLI runs ~167 simulation steps per frame at 60 FPS (MORE than GUI!).

#### At 15% Throttle, What's Different About the Timing?

**Answer**: **Nothing is different about the timing!** The timing is identical or even better in CLI.

**THE REAL PROBLEM**: At 15% throttle, the issue is NOT timing - it's **exhaust flow magnitude**.

---

## Root Cause Hypothesis

### The Problem is NOT:

1. ❌ Audio thread not running (CLI correctly starts audio thread)
2. ❌ Missing thread coordination (CLI follows same pattern as GUI)
3. ❌ Insufficient simulation steps (CLI runs MORE steps than GUI)
4. ❌ Incorrect audio buffer reading (CLI uses same readAudioOutput as GUI)

### The Problem IS:

**INSUFFICIENT EXHAUST FLOW MAGNITUDE AT LOW THROTTLE**

#### Evidence Chain:

1. **Exhaust Flow Calculation** [piston_engine_simulator.cpp:394-398]:
   ```cpp
   double exhaustFlow = attenuation_3 * 1600 * (
       1.0 * (chamber->m_exhaustRunnerAndPrimary.pressure() - units::pressure(1.0, units::atm))
       + 0.1 * chamber->m_exhaustRunnerAndPrimary.dynamicPressure(1.0, 0.0)
       + 0.1 * chamber->m_exhaustRunnerAndPrimary.dynamicPressure(-1.0, 0.0)
   );
   ```

2. **At 15% Throttle**:
   - Manifold pressure is only slightly above atmospheric (1.0 atm)
   - Exhaust flow magnitude is proportional to `(pressure - 1.0 atm)`
   - At low throttle, this difference is VERY SMALL
   - Result: Minimal exhaust flow → minimal audio signal

3. **Governor's Safety Feature** [governor.cpp:43-46]:
   ```cpp
   if (std::abs(currentSpeed) < std::abs(0.5 * m_minSpeed)) {
       m_velocity = 0;
       m_currentThrottle = 1.0;  // ← FULL THROTTLE at low RPM!
   }
   ```
   - Governor AUTOMATICALLY increases throttle to 100% when engine is running slowly
   - This ensures sufficient exhaust flow for audio generation
   - **CLI may not be using Governor correctly or at all!**

4. **CLI Uses `setSpeedControl()` BUT...**
   - If the engine is configured with a basic `Throttle` instead of a `Governor`, the safety feature doesn't exist
   - The CLI sets 15% throttle and maintains it constantly
   - Without Governor's intervention, throttle stays at 15% even at low RPM
   - Result: Insufficient exhaust flow → perceived audio dropout

---

## Recommended Fix

### Option 1: Ensure CLI Uses Governor (Recommended)

**Problem**: CLI may be creating an engine with `Throttle` instead of `Governor`.

**Solution**: Verify that the engine script creates a Governor throttle system.

**Check**:
1. Look at the engine configuration file (main.mr or specified script)
2. Search for throttle/governor initialization
3. Ensure Governor parameters are properly configured

**Example** (from typical engine config):
```piranha
Governor governor;
governor.minSpeed = 1000 rpm;
governor.maxSpeed = 6000 rpm;
governor.k_s = 0.1;
governor.k_d = 0.5;
governor.gamma = 1.0;
engine.throttle = governor;
```

### Option 2: Implement Minimum Throttle in CLI

**Problem**: Even with Governor, 15% requested throttle may be too low for startup.

**Solution**: Add minimum throttle threshold in CLI warmup sequence.

**Code Change** [engine_sim_cli.cpp:788-795]:
```cpp
// Current code:
double warmupThrottle;
if (currentTime < 1.0) {
    warmupThrottle = 0.5;  // Medium throttle for initial start
} else if (currentTime < 1.5) {
    warmupThrottle = 0.7;  // Higher throttle for combustion development
} else {
    warmupThrottle = 0.8;  // High throttle ready for starter disable
}

// Recommended change:
double warmupThrottle;
if (currentTime < 1.0) {
    warmupThrottle = 1.0;  // ← FULL throttle for initial start (Governor safety)
} else if (currentTime < 1.5) {
    warmupThrottle = 1.0;  // ← Maintain full throttle until stable
} else {
    warmupThrottle = std::max(0.8, throttle);  // ← Don't go below 80%
}
```

### Option 3: Add Diagnostic Logging

**Problem**: Need to verify what's actually happening with throttle and exhaust flow.

**Solution**: Add comprehensive logging to CLI.

**Code Addition**:
```cpp
// After EngineSimUpdate [engine_sim_cli.cpp:943]
EngineSimGetStats(handle, &stats);
static int logCounter = 0;
if (logCounter++ % 60 == 0) {  // Log once per second
    std::cout << "DIAG: RPM=" << stats.currentRPM
              << " Throttle=" << throttle
              << " ExhaustFlow=" << stats.exhaustFlow
              << " ManifoldPressure=" << stats.manifoldPressure << "\n";
}
```

---

## Testing Strategy

### Test 1: Verify Governor is Active

**Method**:
1. Add logging to Governor::update() to confirm it's being called
2. Check if m_currentThrottle is being adjusted automatically
3. Verify throttle goes to 1.0 at low RPM

**Expected Result**:
- At RPM < 500, throttle should automatically go to 1.0
- Exhaust flow should increase significantly
- Audio should be present even at low requested throttle

### Test 2: Measure Exhaust Flow at Different Throttles

**Method**:
1. Run CLI with throttle = 0.05, 0.10, 0.15, 0.20, 0.50, 1.0
2. Record exhaust flow values for each
3. Plot exhaust flow vs throttle

**Expected Result**:
- Exhaust flow should be non-linear with throttle (due to manifold pressure dynamics)
- Should see significant drop in exhaust flow below 20% throttle
- This will confirm the "dropout threshold"

### Test 3: Compare GUI vs CLI Throttle Behavior

**Method**:
1. Run GUI with Q key (1% speed setting) - equivalent to low throttle
2. Measure actual throttle applied (via Governor)
3. Run CLI with 15% throttle
4. Compare exhaust flow and audio output

**Expected Result**:
- GUI Governor should automatically increase throttle to 1.0 at low RPM
- CLI should do the same if Governor is active
- If not, this confirms the missing Governor issue

### Test 4: Audio Buffer Analysis

**Method**:
1. Add logging to Synthesizer::renderAudio() to record:
   - Input buffer size before rendering
   - Number of samples rendered
   - Output buffer size after rendering
2. Run at 15% throttle
3. Analyze the logs

**Expected Result**:
- If input buffer is consistently empty → starvation issue
- If output buffer is consistently full → consumer issue
- If samples rendered is very small → magnitude issue (expected at 15%)

---

## Conclusion

### Root Cause

**The CLI is NOT using the Governor's closed-loop feedback system correctly**, resulting in:
1. Fixed 15% throttle even at low RPM
2. Insufficient manifold pressure
3. Minimal exhaust flow magnitude
4. Perceived audio dropouts (silence)

### Why GUI Works

GUI uses `setSpeedControl()` which activates Governor:
- Governor detects low RPM (< 0.5 * minSpeed)
- Automatically sets throttle to 100%
- Ensures sufficient exhaust flow for audio
- Governor's closed-loop feedback continuously adjusts throttle

### Why CLI Has Dropouts

CLI may be:
1. Creating engine with basic Throttle instead of Governor
2. Or not calling Governor::update() properly
3. Result: Fixed 15% throttle regardless of RPM
4. At 15% throttle, manifold pressure is barely above atmospheric
5. Exhaust flow is minimal → minimal audio signal

### The Fix

**Immediate**: Add minimum throttle threshold in CLI warmup (Option 2 above)

**Long-term**: Verify and fix Governor usage in engine initialization (Option 1 above)

**Validation**: Use diagnostic logging to confirm Governor is active and adjusting throttle (Test 1 above)

---

## Appendix: Code References

### Key Files and Line Numbers

| File | Lines | Function | Purpose |
|------|-------|----------|---------|
| synthesizer.cpp | 168-195 | `writeInput()` | Writes exhaust flow to input buffer |
| synthesizer.cpp | 222-256 | `renderAudio()` | Generates audio from input buffer |
| synthesizer.cpp | 215-219 | `audioRenderingThread()` | Audio thread main loop |
| piston_engine_simulator.cpp | 371-413 | `writeToSynthesizer()` | Calculates exhaust flow |
| piston_engine_simulator.cpp | 394-398 | Exhaust flow calc | Flow = attenuation³ × 1600 × ΔP |
| simulator.cpp | 98-156 | `simulateStep()` | Main physics step |
| simulator.cpp | 152 | `writeToSynthesizer()` call | Called once per step |
| governor.cpp | 36-52 | `update()` | Closed-loop throttle control |
| governor.cpp | 43-46 | Low RPM safety | Full throttle at low RPM |
| engine_sim_application.cpp | 800 | `setSpeedControl()` | GUI uses Governor |
| engine_sim_cli.cpp | 797, 941 | `EngineSimSetThrottle()` | CLI throttle control |
| engine_sim_bridge.cpp | 433 | `setSpeedControl()` | Bridge to engine |
| engine_sim_bridge.cpp | 965 | `EngineSimReadAudioBuffer()` | Read from audio thread |

### Configuration Values

| Parameter | GUI | CLI | Impact |
|-----------|-----|-----|--------|
| Audio sample rate | 44100 Hz | 48000 Hz | Audio quality |
| Simulation frequency | ~8000 Hz | 10000 Hz | Physics accuracy |
| Input buffer size | 44100 | 48000 | Input latency |
| Audio buffer size | 44100 | 48000 | Output latency |
| Throttle control | Governor (closed-loop) | Unknown (may be direct) | **CRITICAL** |

---

---

## CRITICAL UPDATE: SMOKING GUN FOUND!

### The Default Engine (Subaru EJ25) Does NOT Use Governor!

**Evidence from Engine Configuration**:

**Subaru EJ25 Configuration** [engines/atg-video-2/01_subaru_ej25_eh.mr:143]:
```piranha
engine engine(
    name: "Subaru EJ25",
    starter_torque: 70 * units.lb_ft,
    starter_speed: 500 * units.rpm,
    redline: 6500 * units.rpm,
    throttle_gamma: 2.0,  // ← Only specifies gamma, NO governor!
    hf_gain: 0.01,
    noise: 1.0,
    jitter: 0.5,
    simulation_frequency: 20000
)
```

**Kohler CH750 Configuration** [engines/atg-video-1/02_kohler_ch750.mr:20-29]:
```piranha
engine engine(
    name: "Kohler CH750",
    starter_torque: 50 * units.lb_ft,
    starter_speed: 500 * units.rpm,
    redline: 3600 * units.rpm,
    throttle:
        governor(  // ← EXPLICITLY USES GOVERNOR!
            min_speed: 1600 * units.rpm,
            max_speed: 3500 * units.rpm,
            min_v: -5.0,
            max_v: 5.0,
            k_s: 0.0006,
            k_d: 200.0,
            gamma: 2.0
        ),
    hf_gain: 0.01,
    noise: 1.0,
    jitter: 0.5,
    simulation_frequency: 30000
)
```

### What Happens When No Governor Is Specified?

**Default Throttle System**: `DirectThrottleLinkage` [direct_throttle_linkage.cpp:20-28]
```cpp
void DirectThrottleLinkage::setSpeedControl(double s) {
    Throttle::setSpeedControl(s);
    m_throttlePosition = 1 - std::pow(s, m_gamma);  // ← Simple formula, NO feedback!
}

void DirectThrottleLinkage::update(double dt, Engine *engine) {
    Throttle::update(dt, engine);  // Does NOTHING
    engine->setThrottle(m_throttlePosition);  // ← Just sets fixed throttle
}
```

**Comparison**:

| Feature | Governor (Kohler) | DirectThrottleLinkage (Subaru) |
|---------|-------------------|-------------------------------|
| Closed-loop feedback | ✅ Yes | ❌ No |
| Full throttle at low RPM | ✅ Yes (safety feature) | ❌ No |
| Automatic throttle adjustment | ✅ Yes | ❌ No |
| Throttle calculation | Complex PID-based | Simple: `1 - s^gamma` |

### The Root Cause - CONFIRMED!

**When CLI runs with Subaru EJ25 at 15% throttle:**

1. **CLI calls**: `EngineSimSetThrottle(handle, 0.15)`
2. **Bridge calls**: `engine->setSpeedControl(0.15)`
3. **DirectThrottleLinkage::setSpeedControl(0.15)**:
   ```cpp
   m_throttlePosition = 1 - pow(0.15, 2.0) = 1 - 0.0225 = 0.9775
   ```
   Wait, that's 97.75% throttle! Let me recalculate...

   Actually, looking at the formula more carefully:
   - When s=0.15 (15% speed control), throttle = 1 - 0.15² = 1 - 0.0225 = 0.9775
   - When s=1.0 (100% speed control), throttle = 1 - 1.0² = 0
   - When s=0.0 (0% speed control), throttle = 1 - 0.0² = 1.0

   **This seems backwards!** Let me check the GUI behavior...

**Actually, looking at the GUI code more carefully** [engine_sim_application.cpp:776-800]:
```cpp
if (m_engine.IsKeyDown(ysKey::Code::Q)) {
    m_targetSpeedSetting = 0.01;  // 1% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::W)) {
    m_targetSpeedSetting = 0.1;   // 10% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::E)) {
    m_targetSpeedSetting = 0.2;   // 20% speed
}
else if (m_engine.IsKeyDown(ysKey::Code::R)) {
    m_targetSpeedSetting = 1.0;   // 100% speed
}
```

**The GUI uses speed control values of 0.01, 0.1, 0.2, or 1.0, NOT throttle percentages!**

So when the user presses Q (lowest speed setting):
- `m_targetSpeedSetting = 0.01` (1% of max speed)
- Governor tries to maintain 1% of 6500 RPM = 65 RPM
- Governor applies FULL THROTTLE to get engine running
- Once engine reaches 65 RPM, Governor reduces throttle

**BUT** with DirectThrottleLinkage:
- `m_throttlePosition = 1 - pow(0.01, 2.0) = 1 - 0.0001 = 0.9999` (99.99% throttle)
- This is ALMOST full throttle, which should work!

**WAIT - Let me check what throttle value the CLI is actually setting...**

Looking at CLI code [engine_sim_cli.cpp:797]:
```cpp
EngineSimSetThrottle(handle, warmupThrottle);  // warmupThrottle = 0.5, 0.7, or 0.8
```

And later [engine_sim_cli.cpp:926-936]:
```cpp
else if (args.targetLoad >= 0) {
    // Direct throttle mode
    throttle = args.targetLoad;  // This is the --load value (0 to 1)
}
```

**So when user specifies `--load 15`**: `throttle = 0.15`

Then `DirectThrottleLinkage::setSpeedControl(0.15)` calculates:
```cpp
m_throttlePosition = 1 - pow(0.15, 2.0) = 0.9775  // 97.75% throttle!
```

**This should be plenty of throttle!** So why the dropouts?

**Let me re-examine the CLI throttle setting...**

Actually, I see the issue now! Looking at engine_sim_bridge.cpp:433:
```cpp
// Use the Governor abstraction for proper closed-loop feedback
if (ctx->engine) {
    ctx->engine->setSpeedControl(position);  // position = 0.15
}
```

And DirectThrottleLinkage::setSpeedControl(0.15):
```cpp
m_throttlePosition = 1 - std::pow(0.15, m_gamma);  // m_gamma = 2.0
                     = 1 - 0.0225
                     = 0.9775  // This is what gets set!
```

**But wait - 97.75% throttle should produce PLENTY of exhaust flow!**

Let me check if the issue is that the CLI is NOT calling the throttle update...

**Looking at Engine::update()** - need to find where throttle update is called:

```cpp
// From engine_sim_application.cpp process() function
m_simulator->startFrame(1 / avgFramerate);
while (m_simulator->simulateStep()) {
    // ...
}
m_simulator->endFrame();
```

And in simulateStep() [simulator.cpp:98-156]:
```cpp
m_system->process(timestep, 1);
m_engine->update(timestep);  // ← This should call throttle update!
m_vehicle->update(timestep);
m_transmission->update(timestep);
// ...
simulateStep_();
writeToSynthesizer();
```

**So the throttle update IS being called!** The DirectThrottleLinkage::update() should be setting the throttle to 97.75%.

**Unless...** Let me check if there's something different about how the CLI initializes the engine...

**Actually, I think I found the real issue!**

Looking at the warmup code [engine_sim_cli.cpp:788-795]:
```cpp
double warmupThrottle;
if (currentTime < 1.0) {
    warmupThrottle = 0.5;  // Medium throttle for initial start
} else if (currentTime < 1.5) {
    warmupThrottle = 0.7;  // Higher throttle for combustion development
} else {
    warmupThrottle = 0.8;  // High throttle ready for starter disable
}

EngineSimSetThrottle(handle, warmupThrottle);
```

With DirectThrottleLinkage and gamma=2.0:
- warmupThrottle=0.5 → actual = 1 - 0.5² = 0.75 (75%)
- warmupThrottle=0.7 → actual = 1 - 0.7² = 0.51 (51%)
- warmupThrottle=0.8 → actual = 1 - 0.8² = 0.36 (36%)

**These are reasonable throttle values that should produce audio!**

**So the issue must be something else...**

Let me check if the CLI is actually different from GUI in some other way...

**Actually, I think the real issue is that the user is testing with `--load 15` which means 15% load, NOT 15% throttle!**

When `--load 15` is specified:
```cpp
args.targetLoad = 15.0 / 100.0 = 0.15  // 15%
```

Then in the main loop:
```cpp
else if (args.targetLoad >= 0) {
    throttle = args.targetLoad;  // throttle = 0.15
}
```

With DirectThrottleLinkage and gamma=2.0:
```cpp
m_throttlePosition = 1 - pow(0.15, 2.0) = 0.9775  // 97.75% throttle
```

**So even with `--load 15`, the actual throttle is 97.75%!**

**This means the issue is NOT throttle-related!**

Let me reconsider... Maybe the issue is:
1. The engine isn't actually starting (starter motor issues?)
2. The audio thread isn't running properly
3. There's a buffer underrun issue
4. The exhaust flow calculation is different than I thought

**Let me check the starter motor behavior...**

Looking at CLI [engine_sim_cli.cpp:809-837]:
```cpp
EngineSimSetStarterMotor(handle, 1);  // Enable starter

while (running) {
    const double minSustainedRPM = 550.0;
    if (stats.currentRPM > minSustainedRPM && EngineSimSetStarterMotor(handle, 0) == ESIM_SUCCESS) {
        // Disable starter at 550 RPM
    }
}
```

**And the GUI** [engine_sim_application.cpp:859-865]:
```cpp
if (m_engine.IsKeyDown(ysKey::Code::S)) {
    m_simulator->m_starterMotor.m_enabled = true;
}
else {
    m_simulator->m_starterMotor.m_enabled = false;
}
```

**AH HA!** The GUI uses the S KEY to control starter motor manually, while the CLI tries to auto-disable it!

**But that still doesn't explain why audio works in GUI and not in CLI...**

**Let me check one more thing - is the audio thread actually being started in CLI?**

Yes! [engine_sim_cli.cpp:702]:
```cpp
result = EngineSimStartAudioThread(handle);
```

**And is EngineSimReadAudioBuffer being called correctly?**

Yes! [engine_sim_cli.cpp:965]:
```cpp
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**So everything looks correct...**

**UNLESS** - Let me check if there's a timing issue. The CLI reads audio immediately after update, but maybe the audio thread hasn't had time to process the input yet?

Looking at the bridge code [engine_sim_bridge.cpp:602-612]:
```cpp
// IMPORTANT: This function does NOT call renderAudio()
// It only reads from the audio buffer that the audio thread is filling
int samplesRead = ctx->simulator->readAudioOutput(
    frames,
    ctx->audioConversionBuffer
);
```

**This should work** - the audio thread is continuously filling the buffer, and readAudioOutput just reads from it.

**I'm stumped. Everything looks correct in the code.**

**HYPOTHESIS**: The issue might be that the engine never actually starts running (stays at 0 RPM) due to some initialization issue, so there's no exhaust flow, hence no audio.

**Next debugging step**: Add logging to verify:
1. Engine RPM is actually increasing
2. Starter motor is actually applying torque
3. Combustion is actually happening
4. Exhaust flow is being generated

---

**END OF REPORT**

**Next Steps**:
1. Add comprehensive logging to CLI to verify engine is actually running
2. Test with Kohler engine (which uses Governor) to see if it works better
3. Compare actual throttle values between GUI and CLI using logging
4. Verify audio thread is actually producing samples by checking buffer levels
