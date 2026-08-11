# Build Scripts

Helper scripts for development and CI.

## `bootstrap.sh`

One-command fresh-clone setup. Installs dependencies and builds.

```bash
./scripts/bootstrap.sh
```

**What it does:**
1. Validates macOS
2. Installs CMake, Bison, Flex via Homebrew
3. Runs `make`

## `test-build.sh`

Full build validation suite. Use after making changes or before submitting PRs.

```bash
./scripts/test-build.sh
```

**What it tests:**
- CMake configure succeeds without Boost
- Bison/Flex dependency warnings appear when tools missing
- Compiler flag compatibility (handles old/new Clang differences)
- Full build completes
- Binary runs and generates valid WAV output
- Default engine configuration loads correctly

The script exits with code 0 on success, non-zero on any failure. All output is logged to `build/cmake.log` and `build/build.log` for debugging.

## `afterfire_smoke.sh`

Non-interactive acceptance check that exhaust **afterfire pops actually fire**.
No keyboard, no human, no audible output.

```bash
./scripts/afterfire_smoke.sh                          # uses build-cli/engine-sim-cli
./scripts/afterfire_smoke.sh --binary build/engine-sim-cli
make afterfire-smoke                                  # same, via the Makefile
```

**How it proves it:**

A pop requires *overrun* — throttle at/below the cutoff while RPM is high and
falling. `--throttle` latches one constant value for the whole run, so it can
never express that transition. The script therefore generates a timecoded
telemetry trace (`scripts/fixtures/afterfire_rev_cut.csv`) and replays it:

| phase   | throttle | purpose                            |
|---------|----------|------------------------------------|
| 0–2s    | 0%       | crank and catch (`--start` implicit for replay) |
| 2–7s    | 100%     | rev up                             |
| 7–13s   | 0%       | **the cut** — RPM coasts down, pops fire |

It then parses the `--afterfire-diagnostics` block and asserts `events > 0`.
`--afterfire-probability 1.0` makes the probability gate deterministic so the
check is a stable pass/fail rather than a coin flip.

**Exit codes:** `0` = pops fired · `1` = no pops (the printed skip counters name
the gate that rejected every candidate) · `2` = setup failure (missing binary,
script, or impulse-response WAVs).

Not part of `make test`: the run drives 13s of trace through the full physics
pipeline and takes minutes, which is too slow for the inner loop.
