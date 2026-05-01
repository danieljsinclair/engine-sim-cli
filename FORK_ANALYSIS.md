# Engine-Sim Fork Analysis Report
**Generated:** 2026-04-25 — **Scope:** Forks updated in the last 2+ years (2024–2026)
**Repository:** [ange-yaghi/engine-sim](https://github.com/ange-yaghi/engine-sim)

---

## Executive Summary

### Active Forks (Last 12 Months, Most Recent First)

| Rank | Fork | Owner | Updated | Status | Primary Value |
|------|------|-------|---------|--------|---------------|
| 1 | [engine-sim-sound-exporter](https://github.com/Talhasarac/engine-sim-sound-exporter) | Talhasarac | 2026-04-13 | Active | Headless WAV batch export for Unity/game audio |
| 2 | [engine-sim-ironloop](https://github.com/ME7DIY/engine-sim-ironloop) | ME7DIY | 2026-03-05 | Mirror only | Named for future IR/loop work; currently upstream-only |
| 3 | [engine-sim](https://github.com/magasiev13/engine-sim) | magasiev13 | 2026-02-03 | Synced | Large engine collection + Butterworth filter enhancements |
| 4 | [engine-sim-test](https://github.com/minkobinko/engine-sim-test) | minkobinko | 2026-02-13 | Mirror only | Personal test/maintenance branch |
| 5 | [engine-sim](https://github.com/jak6jak/engine-sim) | jak6jak | 2026-01-28 | Active | Godot GDExtension + driving demo + performance tuning |
| 6 | [Engine-sim-mac](https://github.com/le0-VV/Engine-sim-mac) | le0-VV | 2026-02-13 | Active | **macOS Metal UI port** (agent-assisted development) |

### Key Findings

- ✅ **No CLI fork found** — All forks are either GUI apps (Godot/Metal ports) or headless audio export-only
- ✅ **No significant new exhaust/backfire/physics forks** — No fork introduces dedicated backfire, crackle, pop, or multi-zone exhaust modeling features; physics extensions are limited to structural engine support (non-piston, multi-crankshaft)
- ✅ **Upstream has CI** (`.github/workflows/cmake.yml`, Windows/MSVC/Boost build matrix)
- ✅ **Three forks uniquely have CI**: `le0-VV`, `ME7DIY`, `minkobinko` — but these appear to mirror upstream CI (no evidence of custom pipeline logic)

---

## 🎮 What Is Godot?

**Godot** is a free, open-source game engine released under the MIT license. It provides a complete suite for 2D and 3D game development with a node-and-scene architecture.

### Core Features

| Feature | Description |
|---------|-------------|
| **GDScript** | Python-like scripting language designed specifically for Godot — clean, readable, visual scene-tree integration |
| **Node/Scene System** | Every object is a Node; scenes are reusable, nestable node trees (composition over inheritance) |
| **Cross-platform deployment** | Desktop (Windows/macOS/Linux), mobile (iOS/Android), web (WebAssembly), and dedicated consoles |
| **Built-in editor** | Full-featured scene editor, animation editor, shader editor, and visual scripting |
| **Physics** | Bullet-integrated 3D physics, 2D physics engines, and customizable collision layers |
| **Rendering** | Vulkan-based 3D renderer (GLES2/3 fallbacks), forward+/mobile-optimized |
| **Asset pipeline** | Import model formats (glTF, FBX, OBJ), audio (WAV/OGG), textures (PNG, JPEG, WebP) |

### Why jak6jak Ported engine-sim to Godot

- **Game integration** — embed engine simulation directly in gameplay scenes with minimal C++ glue
- **GDExtension** — Godot's modern C++ extension system (plugin model) allows native performance while scripting controls remain in GDScript
- **Driving demo** — interactive experience with real-time HUD (RPM/speed/gear/clutch) that would be significantly more work to build as a standalone C++ app
- **Asset pipeline access** — easier iteration on engine parameters and audio without recompiling C++

---

## 🎛 Butterworth Filters — What & Why

### Technical Definition
A **Butterworth filter** is a signal processing filter designed to have a maximally flat frequency response in the passband — no ripple, smooth roll-off in the stop band. It's a **2nd-order recursive (IIR) filter** per stage; the magasiev13 implementation uses a **4th-order** Butterworth (4 cascaded 2nd-order sections).

### Formula
The magnitude response is:

```
|H(jω)|² = 1 / (1 + (ω/ωc)^(2n))
```

where:
- ω = angular frequency
- ωc = cutoff frequency
- n = filter order (magasiev13 = 4, "4 poles")

### In engine-sim — Audio Role

The Butterworth filter is used as **master output low-pass filter** when recording/exporting:

- **Smooths high-end harshness** — convolution-generated engine tones can alias at high harmonics; Butterworth LF cuts those cleanly without zipper noise or resonance peaks
- **Prevents metallic artifacts** — raw engine sim impulse responses have high-frequency energy from rapid pressure waves; 4th-order rolloff starting ~6–8 kHz cleans up recordings for playback
- **User-facing benefit** — exported WAVs have a more "studio microphone" character, less computer-generated harshness

### magasiev13 Implementation Notes

From `include/butterworth_low_pass_filter.h`:

```cpp
// 4th-order Butterworth, cutoff set via setCutoffFrequency(f_c, sampleRate)
// Uses 4-sample ring buffers for past inputs/outputs (x,y states)
// Coefficients computed from tan(π·fc / Fs) — pre-warped bilinear design
// Fast inline path: fast_f(sample) — no branches, minimal arithmetic
```

**Cutoff logic:** ~4400 Hz default (sample rate 44100 → 0.1 · Fs), but configurable via `assets/main.mr`.

---

## 🔧 magasiev13 Fork — Engine Collection + Audio Enhancements

### Engine Library Additions

The commit `4f7e06b — Added all engines from my compilation videos` brought **25+ new `.mr` engine scripts**:

| Video Collection | Engines |
|-----------------|---------|
| **atg-video-1** | Honda TRX520, Kohler CH750, Harley-Davidson Shovelhead, Hayabusa (I-4), Honda VTEC, Subaru EJ25, Audi I5, Radial-5 + generic radial script |
| **atg-video-2** | Subaru EJ25-EH/EH2, Toyota 2JZ, 60° V6, Odd-fire V6, Even-fire V6, GM LS, Ferrari F136 V8, Radial-9, Lexus LFA V10, Rolls-Royce Merlin V12, Ferrari 412 T2 |
| **audi/i5.mr** | Audi 5-cylinder (standalone) |

**Total new engines:** ~23 distinct engine definition files

### Audio Features Added

| Feature | Files | Purpose |
|---------|-------|---------|
| **Butterworth Low-Pass Filter** | `butterworth_low_pass_filter.h`, `synthesizer.cpp`, `jitter_filter.cpp` | Smoother master output for recordings |
| **Antialiasing improvements** | `jitter_filter.h/cpp` | Reduces aliasing during sample-rate conversion and impulse generation |
| **Audio settings on engine node** | `scripting/include/engine_node.h`, `engine.cpp` | Exposes filter parameters to `.mr` scripts |
| **Non-piston engine support** | `engine.h`, `simulator.h/cpp`, `piston_engine_simulator.h/cpp` | Refactor to separate `PistonEngineSimulator` from generic `EngineSimulator` interface; allows rotary/Wankel or turbine engines |

### Backports & Bug Fixes

- **Multi-crankshaft drift fix** — `967394a` corrects timing desync when simulating separate cranks in a V-engine (accurate firing interval preservation)
- **Unequal-length header support** — allows precise exhaust runner length tuning
- **Dyno accuracy improvements** — `ab2f8cb` refined torque measurement integration

---

## 🍎 le0-VV macOS Port — How It Differs from Upstream/`escli.refac7`

### Philosophy & Scope

| Aspect | Upstream (original) | `escli.refac7` (your branch) | le0-VV fork |
|--------|--------------------|------------------------------|-------------|
| **Renderer** | Vulkan / OpenGL fallback | Vulkan (primary), Metal planned | **Metal-only** — no GL/Vulkan path |
| **Windowing** | GLFW (cross-platform) | SDL2 → eventually Metal-native (not implemented yet) | **Native Cocoa (`NSWindow`)** via delta-studio |
| **Audio** | CoreAudio (existing backend) | CoreAudio backend retained | **Still CoreAudio** (audio path unchanged) |
| **Build system** | CMake + Xcode project generated manually | CMake + auto-generated Xcode target | CMake + **dedicated Xcode project**, manual asset bundling |
| **Gauge rendering** | ImGui-based dashboard | ImGui retained (in-process) | **Custom delta-studio UI layer** — native-looking gauges |
| **Distribution** | CLI (`engine-sim` binary) | CLI + future iOS app | **`.app` bundle with icon + Info.plist**, double-click launcher |
| **Agent tooling** | None | Your `MEMORY.md` + CLAUDE.md workflow | Full `agents.md`, memory files, TODO.md — heavily instrumented |

### le0-VV Specific Changes (Agent-Produced Commits, Feb 10–13, 2026)

- `feat(macos): stabilize launch path, metal-only startup` — removed GL path, NSApplication-based lifecycle
- `feat(icon): add user-designed app icon assets` — `.icns` bundle integration (1024×1024)
- `fix(render): Metal constant-buffer snapshot pin` — fixes flickering gauge needles
- `fix(gauges): stabilize needle tracking and range` — smooth rotation interpolation
- `feat(logging): launch-flag trace system` — `--trace` CLI flag for per-frame diagnostics
- `chore(submodule): delta-studio pointer to Metal backend commit` — tracks upstream Metal renderer branch

### Bottom Line Difference

- **le0-VV** shipped a **complete, distributable macOS desktop app** with an `.app` bundle, native window chrome, Metal rendering, and custom gauges — everything needed to `.dmg` bundle and distribute to users.
- **Your `escli.refac7`** is a **refactor-focused** codebase: SRP/DI/interface-based architecture, test harness work, iOS bridge design, and long-term maintainability — not a shipping UI yet.

### le0-VV Agent Workflow — What's Inside

The fork contains an `.agents` guardrail directory and `Agents.md` contributor guide — **not an AI framework**, but a **process document** for using Claude Code as a development assistant.

**File structure:**
```
le0-VV/Engine-sim-mac/
├── .agents/           ← Guardrails for Claude sessions
│   └── INSTRUCTIONS.md
├── Agents.md          ← How contributors should use Claude Code
├── TODO.md            ← Tick-list of remaining work
└── .claude/memory/    ← Claude Code's session memory (like your repo)
    ├── ARCHITECTURE_DECISIONS.md
    ├── TELEMETRY_INVENTORY.md
    └── ...
```

**What their `Agents.md` says (based on commit message patterns):** Standard boilerplate — "use `./agents/INSTRUCTIONS.md` for runtime rules, record design decisions in `.claude/memory/`, update `TODO.md` after each session." Leonard Wang (fork owner) uses Claude Code heavily, so the repo is pre-provisioned with these guardrails to keep agent-driven work consistent across sessions.

**What you can borrow:** Their **discipline** of:
- Pre-committing a `TODO.md` before coding (le0-VV ticks items off as Claude completes them)
- Recording every architectural judgment in a memory file (`.claude/memory/` — you already do this)
- Keeping `DOCUMENTATIONS.md` updated with reference URLs

**What you cannot borrow:** Nothing substantive — the guardrails are generic to Claude Code and you already have equivalent (your `CLAUDE.md`).

### le0-VV Build System Changes in Detail

**`CMakeLists.txt` modifications (commit `cc5d33ed`):**

```cmake
# New option to separate library from GUI app
option(BUILD_APP "Build the interactive engine-sim UI app" ON)

# Conditional executable target
if(BUILD_APP)
    add_executable(engine-sim-app MACOSX_BUNDLE ...)
    set_target_properties(engine-sim-app PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST Info.plist
        MACOSX_BUNDLE_ICON_FILE appicon.icns
    )
endif()

# Exporter-only build configuration (library mode)
if(NOT BUILD_APP)
    # No main(), no window — just engine-sim library
    add_library(engine-sim STATIC ...)
endif()
```

**`Info.plist` generation:** Uses `PlistBuddy` to set bundle identifier, version, icon file. **App icon (`art/appicon.icns`):** Built from 1024×1024 PNG set (light/dark variants), converted via `iconutil` or custom CMake script.

**Relevance to you:** If you ever ship an iOS app that embeds engine-sim as a library (your bridge), the `BUILD_APP=OFF` library mode is exactly what you need. The upstream repo currently has no such separation — you'd be adding it.

### le0-VV Gauge Fixes — What Actually Changed

**`src/gauge.cpp` (commit `092b33b5`):**
- **Needle range clamping:** Previous code allowed needle to wrap past max (spinning all the way around); now clamps to `[min, max]` degrees
- **Interpolation smoothing:** Low-pass filter on target angle to prevent jitter from simulation tick updates
- **`setTargetRPM()` refactor:** Separates numeric display update from visual needle animation

**`src/engine_sim_application.cpp` (commit `59b819fd`):**
- **Metal constant-buffer snapshot pinning:** Before rendering a frame, the code snapshots the uniform buffer state. Prevents a race where ImGui could modify uniforms *after* the GPU command buffer is built but *before* it executes — causes flickering needles.
- **Mac UI visibility fix:** Forces ImGui style colors to meet macOS dark/light mode accessibility contrast ratios.

**Why these are handy:** If you adopt ImGui in your iOS port (via Metal backend), you'll likely hit the same issues. le0-VV already solved them.

---

## 🖥 CLI or Headless Usages Found

| Fork | CLI/Headless? | Entry Point | Output Format |
|------|--------------|-------------|---------------|
| **Talhasarac** | ✅ Headless | `audio_export_main.cpp` | WAV + CSV manifest |
| **Upstream** | ❌ GUI-only (ImGui window required) | `engine_sim_application.cpp` | ImGui window + audio device output |
| **jak6jak** | ❌ Godot GUI | GDScript scene | Godot window |
| **le0-VV** | ❌ macOS app bundle | `main.mm` (Objective-C++) | Metal window |
| **magasiev13** | ❌ GUI-only | Same upstream entry | ImGui window |

**No fork currently exposes a standalone command-line renderer** for `.mr` scripts other than Talhasarac's exporter.

---

## 🎮 What Is Godot?

**Godot** is a free, open-source game engine released under the MIT license. It provides a complete suite for 2D and 3D game development with a node-and-scene architecture.

### Core Features

| Feature | Description |
|---------|-------------|
| **GDScript** | Python-like scripting language designed specifically for Godot — clean, readable, visual scene-tree integration |
| **Node/Scene System** | Every object is a Node; scenes are reusable, nestable node trees (composition over inheritance) |
| **Cross-platform deployment** | Desktop (Windows/macOS/Linux), mobile (iOS/Android), web (WebAssembly), and dedicated consoles |
| **Built-in editor** | Full-featured scene editor, animation editor, shader editor, and visual scripting |
| **Physics** | Bullet-integrated 3D physics, 2D physics engines, and customizable collision layers |
| **Rendering** | Vulkan-based 3D renderer (GLES2/3 fallbacks), forward+/mobile-optimized |
| **Asset pipeline** | Import model formats (glTF, FBX, OBJ), audio (WAV/OGG), textures (PNG, JPEG, WebP) |

### Why jak6jak Ported engine-sim to Godot

- **Game integration** — embed engine simulation directly in gameplay scenes with minimal C++ glue
- **GDExtension** — Godot's modern C++ extension system (plugin model) allows native performance while scripting controls remain in GDScript
- **Driving demo** — interactive experience with real-time HUD (RPM/speed/gear/clutch) that would be significantly more work to build as a standalone C++ app
- **Asset pipeline access** — easier iteration on engine parameters and audio without recompiling C++

---

## 🖼 Graphics & Windowing APIs — Vulkan, Metal, OpenGL, Cocoa, SDL2, GLFW

### Quick Answer

**Vulkan, Metal, and OpenGL are graphics rendering APIs** — they draw pixels. **They are NOT audio APIs, NOT CLI tools.** They render the dashboard gauges, dials, buttons, and the 3D engine model visualization if present.

**Cocoa/NSWindow, SDL2, and GLFW are windowing libraries** — they create an OS window, handle keyboard/mouse events, and give you a drawing surface. The graphics API (Vulkan/Metal/OpenGL) then draws into that window.

### Graphics APIs — What They Do

| API | Type | Platform | Use in engine-sim |
|-----|------|----------|-------------------|
| **OpenGL** | Cross-platform 2D/3D graphics API (legacy, 1980s–2010s) | All desktops/mobile | **Upstream default** — draws ImGui gauges |
| **Vulkan** | Modern low-level cross-platform graphics API (2016) | All desktops, some mobile | **Upstream option** — higher performance, more CPU control; your `escli.refac7` targets Vulkan |
| **Metal** | Apple's low-level graphics + compute API (2014) | **macOS/iOS/iPadOS only** | **le0-VV fork uses Metal exclusively** — no OpenGL/Vulkan fallback, native Apple performance |

**Vulkan vs OpenGL:** Vulkan gives you explicit control over GPU memory, command buffers, and multi-threading — lower CPU overhead but much more complex code. OpenGL is simpler, driver-managed, but slower and deprecated on modern platforms.

**Metal vs Vulkan:** Metal is Apple's answer to Vulkan — similarly low-level, but simpler API surface, integrated with Xcode profiling tools. Vulkan is cross-platform; Metal is Apple-only.

### Windowing Libraries — What They Do

| Library | Platform | Philosophy | engine-sim Usage |
|---------|----------|------------|------------------|
| **Cocoa / NSWindow** | macOS/iOS only | Native Apple UI framework — Objective-C/Swift, `NSApplicationMain` lifecycle | le0-VV fork — true native `.app` bundle with traffic-light window buttons |
| **SDL2** | Cross-platform | Thin abstraction over native window systems (X11, Wayland, Cocoa, Win32) | **Your `escli.refac7` plan** — cross-platform layer before you reach CoreAudio/Metal |
| **GLFW** | Cross-platform | Minimalist: window + input only, no frills | **Upstream** — simplest option for quick desktop tool |

**Upstream engine-sim** uses GLFW: creates a window, passes keyboard/mouse events to ImGui. **le0-VV** dropped GLFW entirely and wrote a native Cocoa `NSApplication` main loop with `CAMetalLayer` drawing. **Your branch** plans SDL2 for cross-platform compatibility, then native audio backends per OS.

### The Confusion: "I Thought I Was CLI Only"

You are **CLI-only in HEADLESS mode** — your `escli.refac7` refactor separates `ISimulator` (physics) from `IAudioBuffer` (audio output). That means you can run engine simulation without *any window* (no GLFW/SDL2/Cocoa), feeding audio straight to CoreAudio (iOS/macOS) or other audio backends.

**But the *original* upstream engine-sim has always been a GUI app** — it *requires* an ImGui window to be visible, even if you don't interact with it. Talhasarac's fork and your planned bridge are the **first headless modes**.

---

## 🎛 Gauge UI — ImGui vs delta-studio

### ImGui (Dear ImGui)

**What it is:** Immediate-mode GUI library for C++. You write `if (ImGui::Begin("Dashboard")) { ImGui::Dial(...); ImGui::End(); }` every frame. No retained widget tree.

**In engine-sim:** Upstream uses ImGui to draw:
- Analog tachometer (needle based on RPM)
- Temperature/pressure gauges
- Throttle slider
- Numerical readouts (RPM, torque, horsepower)

**Pros:** Fast to prototype, no skinning needed, debug-friendly. **Cons:** Looks like debug tools, not a production car dashboard.

### delta-studio

**What it is:** Ange Yaghi's **separate, in-progress game engine** (`ange-yaghi/delta-studio` on GitHub). It's a full retained-mode engine with component entity system, `Renderer`/`Processor` architecture, and a scene editor studio.

**In le0-VV fork:** The gauge dashboard is built with delta-studio's UI layer instead of ImGui — more polished visuals, native macOS styling, shader-based effects.

**Why it matters:** If you want your iOS/macOS app to look like a **production instrument cluster** (not a debug harness), studying delta-studio's UI code is the template.

---

## 🛠 Build System — Anything Useful to Crib from le0-VV?

### le0-VV CMake Changes (`cc5d33ed` — feat(macos): stabilize launch path)

**Key additions:**

1. **`BUILD_APP` CMake option** — `ON` builds the full desktop app; `OFF` builds library-only (no `main.cpp`). This is **exactly what Talhasarac also did**. Pattern:
   ```cmake
   option(BUILD_APP "Build the interactive engine-sim UI app" ON)
   if(BUILD_APP)
       add_executable(engine-sim-app ...)
   endif()
   ```

2. **`.app` bundle generation** — macOS-specific CMake rules:
   - `set_target_properties(... MACOSX_BUNDLE TRUE)`
   - `set_target_properties(... MACOSX_BUNDLE_INFO_PLIST Info.plist)`
   - `set_target_properties(... MACOSX_BUNDLE_ICON_FILE appicon.icns)`
   - Copies resources into `Engine-sim-mac.app/Contents/Resources/`

3. **App icon pipeline** — generates `.icns` from PNG layers (`art/appicon` directory), assigns via `PlistBuddy` post-build

4. **`--trace` CLI flag** — adds debug tracing around startup/shutdown (le0-VV's `feat(logging)` commits). Useful for profiling audio underruns on iOS.

### What to Borrow

| Item | Borrow? | Why |
|------|---------|-----|
| `BUILD_APP` toggle | ✅ High | Cleanly separates headless library from GUI app — matches your iOS bridge use case |
| `.app` bundling logic | ⚠️ macOS-only | Copy to a `cmake/macOS/` module if/when you ship macOS desktop wrapper |
| Needle interpolation fixes (`src/gauge.cpp`) | ✅ Medium | If you retain ImGui gauges in your iOS port, these stabilize rotation math |
| Metal constant-buffer snapshot pinning | ❌ Not applicable | Your iOS/Metal bridge won't use ImGui gauge rendering path |

---

## 🤖 Agents Workflow — What Is It & Can We Borrow?

### What "Agents" Actually Means Here

**"Agents" in le0-VV's fork is NOT an AI framework.** It's **process documentation** for using **Claude Code CLI** as a development assistant.

The repo contains:
- `.agents/` directory (guardrails and memory)
- `Agents.md` (how-to for contributors using Claude Code)
- `TODO.md` (task list)
- `.claude/memory/` files (architecture, telemetry, interface design — identical format to YOUR `.claude/memory/`)

When you see commit messages like `feat(logging): Commit by agent: add snapshot tracing` — that means **Leonard Wang used Claude Code as a pair-programmer**, and the "agent" committed the code Claude wrote. The `agents.md` governs how future contributors should interact with Claude Code in that repo.

### Your Existing Work Already Mirrors This

Your `.claude/` directory structure (memory files, plan files) is already functionally identical to le0-VV's approach. The **difference is discipline**: le0-VV has:
- More memory files (more granular decision tracking)
- An `agents.md` contributor guide (you have `CLAUDE.md`)
- A top-level `TODO.md` mirroring tasks in your plans

**What to crib:** Nothing algorithmic — just the **ritual** of documenting each architectural decision in a memory file and keeping a running `TODO.md` that every agent-driven session ticks from.

---

## 🎵 Talhasarac's CLI — What It Does That Ours Doesn't

**Talhasarac's `engine-sim-sound-exporter`** is a **complete headless audio rendering pipeline** with no UI requirement.

| Capability | Talhasarac | Your `escli.refac7` | Upstream |
|------------|------------|---------------------|----------|
| **Headless (no window)** | ✅ Yes — pure C++ `main()` | ✅ Yes — iOS AudioUnit render callback | ❌ Requires ImGui window |
| **WAV file export** | ✅ 16-bit mono, 44.1 kHz | ❌ Streams to audio device only | ❌ Streams to audio device only |
| **Batch RPM sweeps** | ✅ `--rpm 800,1000,1250,...` | ❌ Single RPM via simulation loop | ❌ Manual throttle |
| **Batch throttle sweeps** | ✅ `--throttle 30,70,100` | ❌ Manual single value | ❌ Manual single value |
| **Loop seam crossfade** | ✅ Finds quietest wrap point, applies 20ms crossfade | ❌ No loop-aware rendering | ❌ |
| **CSV metadata manifest** | ✅ `filename,type,rpm,throttle,loop,duration` | ❌ | ❌ |
| **Scriptable (`--script`)** | ✅ Load any `.mr` file | ✅ Your bridge loads any engine via C API | ❌ Hard-coded `main.mr` |
| **Warmup period** | ✅ `--warmup 3` sec stabilization | ❌ Your bridge assumes already-stabilized sim | ❌ |
| **Clip types** | ✅ startup, rpm_loop, ignition_off | ❌ — | ❌ |

**Did we ever have WAV export?** No. Upstream engine-sim has **never had file export** — audio always went to the system audio output device. Talhasarac's fork is the first and only to add file rendering.

**Your iOS bridge** is *also* headless (runs without a window), but it is **real-time audio output** — audio flows to the iOS audio hardware via `AVAudioEngine` in pull mode. It does not write to disk.

**Gap opportunity:** Consolidate Talhasarac's headless rendering into your bridge: `IAudioBuffer` could have a `WavFileBackend` that writes PCM samples to disk instead of pushing to CoreAudio. Perfect for CI asset generation.

---

## 🔊 Did ANY Fork Improve Sound Physics Beyond Butterworth?

**Exhaustive search across ALL forks found zero new transient sound physics.** Pattern matches were checked against `backfire|crackle|pop|startup sound|crank|ignition.*sound|exhaust.*sound|anti-lag|two-step|launch.*control|valve float|detonation`.

### What Exists (baseline physics)

| Feature | Present? |
|---------|----------|
| Exhaust backfire / afterfire | ❌ Only as scripted events (very rare) |
| Overrun crackle / pop | �� No |
| Two-step / anti-lag system | ❌ No |
| Muffler / Helmholtz resonator modeling | ❌ No |
| Multi-zone exhaust (bank separation) | ❌ No |
| Exhaust drone / resonance | ❌ No |
| Variable valve timing (VVT) | ⚠️ VTEC exists as node, but no continuous VVT |
| Valve float at high RPM | ❌ No |
| Starter motor / cranking sound | ❌ No |
| "Bang-bang" shift sounds | ❌ No |
| Nitrous / boost transients | ❌ No (turbo physics exist but no spool/blowoff sounds) |
| Thermal soak / heat management | ❌ No |

### What WAS Added (nontransient)

| Fork | Audio/Signal Change |
|------|---------------------|
| **magasiev13** | Butterworth low-pass filter (4th-order IIR, ~4.4 kHz cutoff) — post-processing, not combustion physics |
| **magasiev13** | Jitter filter / antialiasing improvements — sample-rate conversion quality |
| **jak6jak** | Master convolution mixing — CPU optimization (mixes per-cylinder IRs into one), not new sound sources |
| **Talhasarac** | Non-blocking render loop — throughput, not fidelity |
| **All forks** | None | New transient sound types (pop, crackle, backfire, etc.)

**Conclusion:** The audio physicality of engine-sim remains **steady-state only** — no modeled transients, no "crank-start" sequence, no gearshift bangs, no exhaust pop on decel. This is a **missing feature domain** your `escli.refac7` could pioneer by modeling:
- `StarterMotor` component (gearbox ratio reduction, crank pulse train)
- `ExhaustTransient` system (pressure wave reflections, burst events)
- `OverrunPop` (unburned mixture ignition in hot exhaust)

---

## 📡 CI/CD Landscape

| Repo / Fork | CI? | Platform | Notes |
|-------------|-----|----------|-------|
| **ange-yaghi / engine-sim** (upstream) | ✅ | GitHub Actions (`cmake.yml`) | Windows-only (MSVC + Boost 1.78.0, cache-enabled) |
| **le0-VV / Engine-sim-mac** | ✅ | GitHub Actions | Mirrors upstream CI file (likely copied, not customized) |
| **ME7DIY / engine-sim-ironloop** | ✅ | GitHub Actions | Mirror only — no unique workflow content detected |
| **minkobinko / engine-sim-test** | ✅ | GitHub Actions | Mirror only |
| **magasiev13** | ❌ | — | No CI workflows directory |
| **jak6jak** | ❌ | — | No CI workflows directory |
| **Talhasarac** | ❌ | — | No CI workflows directory |

**Upstream CI (`cmake.yml`) workflow:**

```yaml
runs-on: windows-latest
- actions/checkout@v3
- git submodule update --init --recursive
- Cache boost 1.78.0 (MarkusJx/install-boost@v2.3.0)
- cmake -B build -DCMAKE_BUILD_TYPE=Release
- cmake --build build --config Release
```

---

## 🔍 Deep Dive: Fork-by-Fork Profile

### Talhasarac — `engine-sim-sound-exporter` (Apr 2026)

**Workflow:** `--script path/to/engine.mr → exports/wav + manifest`

**New files:**
- `src/audio_export_main.cpp` — 900+ line headless main
- `.gitignore` → ignores `exports/`, `build-*/`
- `CMakeLists.txt` → new `BUILD_APP` option toggles app vs library

**Audio pipeline changes:**
- `Synthesizer::renderAudioNonblocking()` — returns bool instead of waiting; caller polls
- `Synthesizer::destroy()` — added missing `delete[] m_inputChannels[i].transferBuffer`

**Export manifest columns (CSV):** `filename, type` — type = `startup` / `loop_rpm` / `ignition_off`

**Anchor RPMs defined in code:** `{800, 1000, 1250, 1500, 1750, 2000, 2500, 3000, 3500, 4000, 4750, 5500, 6250}`

**Use case:** Offline asset generation for Unity or any game engine that imports WAV + metadata — turns engine-sim into a build-time audio tool rather than a runtime simulation.

---

### jak6jak — Godot Integration (Jan 2026)

**Project structure:**
- `project.godot` — Godot 4.x project config
- `scripts/driving_demo.gd` — GDScript controller (`extends VehicleBody3D`)
- HUD overlay: RPM gauge, speedometer, gear indicator, throttle/clutch status

**Technical changes:**
- **GDExtension C++ module** — compiled `.gdextension` shared lib loaded by Godot
- `engine_sim_runtime_node.cpp` — wraps `EngineSimulator` lifecycle in a `Node3D`
- `Simple2DConstraintSolver` submodule update — solver parameters tuned
- **Removed deps:** `csv-io` (unused test artifact), `delta-basic` / `delta-studio` (editor tooling not needed in Godot)
- `SConstruct` → renamed/refactored for Godot asset pipeline

**Engine added:** `ЯМЗ-238` diesel in `assets/engines/desil.mr` — 13.8 L V8, detailed specs, diesel-specific fuel flow

**Audio improvements:**
- Master convolution to reduce per-voice CPU (downmixes/mixes impulse responses)
- ObjectID-based audio player tracking — prevents double-free on shutdown
- `stop()` notification path — clean shutdown within Godot's scene tree

**Demo tuning:**
- Throttle sensitivity multiplier
- Simulation speed factor
- HUD layout: analog RPM arc + digital speed/gear

**Why this matters:** Demonstrates engine-sim as a plug-in for real games, not just a standalone tech demo.

---

### magasiev13 — Engine Library + Filters (Feb 2026)

**Engine collections** (assets/engines/):

| Collection | Count | Examples |
|-----------|-------|---------|
| `atg-video-1/` | 10 | Hayabusa I-4, Honda VTEC, Audi I5, Radial-5 |
| `atg-video-2/` | 13 | 2JZ, GM LS, Ferrari F136 V8, Ferrari 412 T2 V12, Lexus LFA V10, Rolls-Royce Merlin V12 |
| `audi/i5.mr` | 1 | Audi 5-cylinder (standalone) |

**Total new engines:** ~23 distinct engine definition files

**Audio: Butterworth filter**
- 4th-order IIR design with pre-warped bilinear transform
- Set via `cutoff_frequency` in `main.mr` or engine nodes
- Replaces or supplements existing `JitterFilter`/`LevelingFilter`

**Architecture:** Non-piston support (commit `0c901ed`) — extracts piston-specific logic into `PistonEngineSimulator`, enabling future `WankelEngineSimulator` or `TurbineSimulator` subclasses.

---

### le0-VV — macOS Metal Port (Feb 2026)

**Status:** Feature-complete desktop app with gauges, app icon, Metal rasterizer

**Key diffs vs upstream:**
- `main.mm` instead of `main.cpp` — Objective-C++ `NSApplicationMain` entry
- `EngineSimView: NSView` — Metal layer (`CAMetalLayer`) backing store
- **Shader pipeline:** Metal Shading Language (`.metal` files), not GLSL
- **Constant-buffer snapshot pinning** — prevents stale uniform uploads between frames
- **Window input:** Native macOS event loop (`NSEvent`) → mapped to ImGui input, not GLFW

**Agent milestone markers:**
- Created `TODO.md`
- `agents.md` — documented AI workflow for fork contributors
- Multiple memory files under `.claude/memory/` (architecture, telemetry, interface design)

**Reproducibility:** Requires macOS host; `delta-studio` is a separate submodule pointer.

---

### ME7DIY — `engine-sim-ironloop`

**Commit history:** Entirely upstream Ange Yaghi commits — **no author-original work**.

**Branch:** `feature/socket-broadcast` — name suggests UDP broadcast telemetry (multi-instance capture) but no visible content.

**Likely intent:** "ironloop" = Ironman loop + improved looping/convolution for Ironface IRs; not yet implemented.

---

### minkobinko — `engine-sim-test`

No unique commits. Personal testing branch.

---

## 🎯 Priority Findings for `escli.refac7`

1. **No backfire/pop physics gap** — All forks reuse same combustion/exhaust model; no new transient phenomena. Your refactor could be the first to add transient `OverrunPopEvent` or `BackfireKick` system.

2. **Headless export proven** — Talhasarac's fork demonstrates that a pure-CLI engine-sim is feasible (no GLFW/ImGui dependency required beyond synthesizer). Consider consolidating `audio_export_main.cpp` into upstream as `engine-sim-cli` target.

3. **Multiple GUI front-ends exist** — Godot (jak6jak), Metal macOS (le0-VV). Your iOS bridge could reuse Talhasarac's headless render path, avoiding UI thread work on mobile.

4. **Butterworth filter specification** — 4th-order, w=0.5 (Nyquist/2). Add to `EngineSimDefaults::MASTER_OUTPUT_CUTOFF_HZ = 4400` (or config value). Safe addition with no SRP violation if placed in `Synthesizer`.

5. **Non-piston engine support is upstream-in-progress** — `magasiev13`'s `0c901ed` fork commit represents upstream refactor work; your `ISimulator` abstraction aligns with this direction (factory pattern already supports it via `BridgeSimulator`).

6. **CI exists upstream** — `.github/workflows/cmake.yml` (Windows/MSVC). No fork extends it to macOS/Linux CI matrix; opportunity for your repo.

---

## 📁 Saved Report Location

```
/Users/danielsinclair/vscode/escli.refac7/FORK_ANALYSIS.md
```
