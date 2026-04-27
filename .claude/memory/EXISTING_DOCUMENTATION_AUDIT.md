# Existing Documentation Audit for VirtualICE Twin Research

## 1. File: /Users/danielsinclair/vscode/escli.refac7/.claude/memory/ARCHITECTURE_DECISIONS.md
- **Relevance**: Core architectural decisions for VirtualICE Twin implementation
- **Key Findings**: 
  - Decision 1: Twin location - Separate `vehicle-twin` module shared by vehicle-sim and bridge
  - Decision 2: Component-based design with interfaces (IVehicleModel, IGearbox, IClutch, IWheel)
  - Decision 3: Evidence-first validation with dual-track approach (dyno-driven vs physics-driven)
  - Decision 18: Phase 1 gearbox uses simple RPM thresholds for shifting
  - Decision 17: MVP clutch model - locked only (pressure=1.0)
  - Decision 19: Dyno used as RPM controller in DynoAssistedTwin
  - Decision 12: Dual-mode strategy pattern for twin implementations
  - Decision 14: Objective acceptance criteria (RPM error < 5%, shift timing within ±200ms)
  - Decision 15: Test isolation via mode switches (--clutch-model, --shift-model, --rpm-mode)
  - Decision 6: Testability via mock telemetry harness
  - Decision 7: Dyno as load parameter consideration
  - Outstanding Unknowns: Clutch constraint behavior, dyno sound realism, wheel torque computation, ZF shift algorithm, Tesla telemetry richness
- **Specialist**: All (Physics Analyst, Gearbox Researcher, Interface Designer, Test Harness Designer)

## 2. File: /Users/danielsinclair/vscode/escli.refac7/docs/BRIDGE_INTEGRATION_ARCHITECTURE.md
- **Relevance**: Architecture for integrating engine-sim CLI with external controllers including VirtualICE Twin
- **Key Findings**:
  - VirtualICE Twin described as simulating petrol car behavior from EV telemetry
  - Inputs: throttle position, gear, clutch, speed, acceleration
  - Outputs: simulated RPM, torque, engine sounds
  - Uses engine-sim's dyno mode for realistic behavior
  - Key Point: ODB2 and VirtualICE are both treated as "upstream" data providers via IInputProvider abstraction
  - Architecture shows UpstreamInputProvider receiving data from ODB2 Adapter or VirtualICE Twin
- **Specialist**: Interface Designer, Test Harness Designer

## 3. File: /Users/danielsinclair/vscode/escli.refac7/.claude/plans/task4-mr-script-survey.md
- **Relevance**: Transmission and vehicle diversity analysis for twin modeling
- **Key Findings**:
  - All .mr scripts model manual transmissions only (zero automatic transmission support)
  - Transmission catalog: 6 gears norm for cars, 5 gears for some vehicles, 1 gear for direct drive
  - Vehicle catalog shows masses ranging from 255kg (Hayabusa) to 1614kg (Corvette/LFA)
  - Gap analysis for automatic transmission: No automatic shift logic, no torque converter model, no shift curve data
  - Critical insight: Automatic transmission simulation likely bridge-only - can run engine-sim in dyno mode and map EV motor RPM/torque to target ICE RPM
  - Realistic shift point data provided for AMG C63, SUV/Crossover, and Sports car
- **Specialist**: Gearbox Researcher

## 4. File: /Users/danielsinclair/vscode/escli.refac7/archive/TA3_FINDINGS.md
- **Relevance**: Threading and synchronization investigation (relevant for audio thread coordination in twin)
- **Key Findings**:
  - Root cause of audio dropouts: CLI not calling `endInputBlock()` after simulation frames
  - Audio thread waits on condition variable `m_cv0` for `!m_processed` flag
  - GUI calls `endFrame()` → `endInputBlock()` → notifies audio thread every frame
  - CLI was missing this critical synchronization call
  - Fix: Add `EngineSimEndFrame()` API call and invoke it after `EngineSimUpdate()`
- **Specialist**: Test Harness Designer

## 5. File: /Users/danielsinclair/vscode/escli.refac7/archive/TA2_FINDINGS.md
- **Relevance**: Physics and audio flow investigation (relevant for understanding engine behavior under load)
- **Key Findings**:
  - Identified critical difference: GUI uses `setSpeedControl()` (Governor with closed-loop feedback) while CLI used `setThrottle()` (though bridge actually calls setSpeedControl)
  - Discovered that Subaru EJ25 engine configuration does NOT use Governor (uses DirectThrottleLinkage instead)
  - Governor provides safety feature: Full throttle at low RPM (< 0.5 * minSpeed)
  - DirectThrottleLinkage lacks closed-loop feedback and low-RPM safety feature
  - Exhaust flow calculation shows strong throttle dependence at low throttle levels
- **Specialist**: Physics Analyst

## 6. File: /Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/README.md
- **Relevance**: Engine simulator documentation showing available controls and dyno functionality
- **Key Findings**:
  - Dyno mode enabled via 'D' key - must be enabled for RPM hold to take effect
  - RPM hold feature measures horsepower and torque at specific RPM
  - Clutch control via Shift key (hold spacebar to slowly engage/disengage)
  - Keys Q,W,E,R change throttle position
  - Simulation time warp keys 1-5
- **Specialist**: Physics Analyst, Gearbox Researcher

## 7. File: /Users/danielsinclair/vscode/escli.refac7/docs/archive/INIT_SEQUENCE_TRACE.md
- **Relevance**: Initialization sequence showing how engine, vehicle, and transmission are created
- **Key Findings**:
  - Shows sequence: Creates engine, vehicle, transmission → Calls simulator->loadSimulation(engine, vehicle, transmission)
  - Confirms that transmission object is passed to simulator during initialization
- **Specialist**: Gearbox Researcher

## 8. File: /Users/danielsinclair/vscode/escli.refac7/docs/BRIDGE_ARCHITECTURE_REVIEW.md
- **Relevance**: Bridge architecture review and static linking plan
- **Key Findings**:
  - Confirms bridge properly wraps engine-sim with static linking
  - Shows architecture diagram with engine-sim components: PistonEngineSimulator, Engine, Vehicle, Transmission, Synthesizer
  - Mentions plans for IEngineProvider interface (though determined YAGNI)
  - Notes that bridge handles both real engine and sine wave scenarios uniformly
- **Specialist**: Interface Designer

## Summary of Specialist Assignments:
- **Physics Analyst**: Should review TA2_FINDINGS.md, engine-sim README.md, and ARCHITECTURE_DECISIONS.md (physics-related decisions)
- **Gearbox Researcher**: Should review task4-mr-script-survey.md, INIT_SEQUENCE_TRACE.md, and ARCHITECTURE_DECISIONS.md (gearbox-related decisions)
- **Interface Designer**: Should review BRIDGE_INTEGRATION_ARCHITECTURE.md, BRIDGE_ARCHITECTURE_REVIEW.md, and ARCHITECTURE_DECISIONS.md (interface-related decisions)
- **Test Harness Designer**: Should review TA3_FINDINGS.md, TA2_FINDINGS.md, and ARCHITECTURE_DECISIONS.md (testing-related decisions)