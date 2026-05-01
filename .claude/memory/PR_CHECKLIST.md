# VirtualICE Twin — Pull Request Quality Gate Checklist

## Purpose

This checklist **must be completed** before any PR is merged into `master`. It enforces the project's architectural standards (SOLID, DRY, TDD, fail-fast, OCP) and ensures code quality, test coverage, and performance regression safety.

---

## 0. Pre-Commit Checklist (Automated Pre-Commit Hook)

Run these before staging files. If any fail, **do not commit**.

- [ ] `git status` shows no unintended files (no `.bak`, no `*.tmp`, no IDE files)
- [ ] Code compiles with **zero warnings** (`cmake --build build --target all -j$(nproc) 2>&1 | grep -i warning` → empty)
- [ ] `clang-format` check passes (`git diff --check` plus style diff shows no formatting changes needed)
- [ ] No `// TODO` or `// FIXME` left in production code (only allowed in test files with issue reference)
- [ ] No commented-out code blocks (use version control for history)
- [ ] All `assert()` uses are for invariants, not input validation

---

## 1. Code Review Items (SOLID / OCP / LSP / ISP / DIP / DRY)

### 1.1 Single Responsibility Principle (SRP)

**Check each modified class/function:**

| Question | Pass Criteria |
|----------|---------------|
| Does this class have **one** primary responsibility? | Yes — describe it in one sentence |
| Are there multiple **reasons** this class might change? | No — if yes, split |
| Does it mix **I/O**, **business logic**, and **data structures**? | No — separate concerns |

**Examples**:
- ✅ `DynoAssistedStrategy` — only computes RPM/gear from telemetry
- ❌ `VirtualIceTwin` that also handles BLE communication and file saving — **FAIL**

### 1.2 Open/Closed Principle (OCP)

| Question | Pass Criteria |
|----------|---------------|
| Is the design **open for extension** (new strategies, new telemetry sources) without modifying existing code? | Yes — via interfaces (`ITwinStrategy`, `ITelemetrySource`) |
| Are `if/else` or `switch` statements used to determine behavior type? | No — use polymorphism/factories instead |
| If adding a new `ITwinStrategy`, do we need to modify `ITwinModel`? | No — only via `switchStrategy()` |

**Red Flags**:
- `if (strategyType == "dyno")` → violates OCP
- Use: `TwinFactory::createStrategy(TwinMode::DynoAssisted)` → OCP compliant

### 1.3 Liskov Substitution Principle (LSP)

| Question | Pass Criteria |
|----------|---------------|
| Can any implementation of `ITwinStrategy` be substituted in `ITwinModel` without breaking correctness? | Yes — all methods honor contracts (esp. `update()` latency, no-throw) |
| Does the subclass strengthen preconditions or weaken postconditions? | No — parameter ranges and return value contracts preserved |
| Are invariant checks present in base and all subclasses? | Yes — e.g., `update()` never throws, always returns valid `TwinOutput` |

**Test**: Write a test that uses `ITwinModel` with a `MockStrategy` — must work identically.

### 1.4 Interface Segregation Principle (ISP)

| Question | Pass Criteria |
|----------|---------------|
| Does any client depend on methods it doesn’t use? | No — interfaces are granular (`IGearbox` separate from `IClutch`) |
| Are there "fat" interfaces with unrelated methods? | No — e.g., `ITwinModel` doesn’t expose `setThrottle()` directly (that’s `IEngineController`) |
| Do we use dependency inversion (program to interfaces, not concretions)? | Yes — `ITwinModel` depends on `ITwinStrategy`, not `DynoAssistedStrategy` |

### 1.5 Dependency Inversion Principle (DIP)

| Question | Pass Criteria |
|----------|---------------|
| Do high-level modules (twin, bridge) depend on abstractions? | Yes — `ITwinStrategy`, `IEngineController`, `ITelemetrySource` are interfaces |
| Do low-level modules (concrete strategies, engine-sim wrapper) depend on abstractions? | Yes — they implement interfaces, not derived from high-level classes |
| Is dependency injection used (not hard-coded `new ConcreteClass` inside logic)? | Yes — factories or constructor injection |

**Violation Example**:
```cpp
// ❌ BAD — hard-coded dependency
class TwinModel {
    DynoAssistedStrategy strategy_;  // concrete!
};

// ✅ GOOD — abstraction
class TwinModel {
    std::unique_ptr<ITwinStrategy> strategy_;
};
```

### 1.6 DRY (Don't Repeat Yourself)

| Question | Pass Criteria |
|----------|---------------|
| Is there duplicated logic that could be extracted? | No — common code is in base classes or free functions |
| Are constants (magic numbers) defined in one place? | Yes — use `EngineSimDefaults`, `ZF_SHIFT_COEFFICIENTS`, etc. |
| Do tests duplicate setup code? | No — use test fixtures (`TEST_F`) |

**Check**:
- `git diff` — are similar blocks repeated? If yes, refactor.
- Same formula appearing twice → extract to helper function.

---

## 2. Test Coverage Requirements

### 2.1 Coverage Targets (New Code Only)

| Category | Minimum Coverage | Notes |
|----------|------------------|-------|
| **Production code** (new/modified) | **≥ 90%** line coverage | Happy path + edge cases |
| **Complex algorithms** (gearbox, clutch slip, RPM calculation) | **≥ 95%** | All branches covered |
| **Error paths** (throws, failure returns) | **100%** of error conditions tested | At least one test per `throw` or error code |

**How to verify**:
```bash
# Generate coverage report
cmake --build build --target coverage
# Or use lcov/gcovr
lcov --capture --directory build --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

### 2.2 Test Types Required

For **each new feature or bug fix**, include:

| Test Type | Description | Example |
|-----------|-------------|----------|
| **Unit test** | Isolated test of single class/function | `TEST_F(SimpleGearboxTest, UpshiftAtCorrectRpm)` |
| **Integration test** | Two+ components working together | `TEST_F(TwinIntegrationTest, StrategyDrivesEngineController)` |
| **RED phase test** | Test that **fails** before fix (TDD) | Initially red, turns green after implementation |
| **Edge case test** | Min/max inputs, boundary conditions | `throttle = 0.0`, `throttle = 1.0`, `dt = 0.001`, `dt = 1.0` |
| **Error condition test** | Verify proper error handling/throwing | `TEST_F(EngineControllerTest, ThrowsOnInvalidGear)` |

### 2.3 Test Quality Criteria

**Do not merge if:**
- Tests use magic numbers without explanation (use named constants)
- Tests have interdependencies (each test must be independent)
- Tests rely on external state (files, network, time) — use mocks
- Assertions test exact error message strings (test exception **type** and key info, not full message)
- Tests are "always passing" (e.g., `EXPECT_TRUE(true)`)

**Do merge if:**
- Tests are deterministic (no `rand()`, no `time(nullptr)`)
- Each test has a clear purpose stated in comment
- Setup/teardown is in fixture (`TEST_F`)
- Edge cases are documented (e.g., "test boundary at redline RPM")

### 2.4 Mock Usage

- Use mocks for `ITelemetrySource`, `IEngineController` in strategy tests
- Use real implementations for integration tests
- **Never** mock what you don't own (e.g., don't mock engine-sim internals — use `ISimulator` interface)

---

## 3. Build Verification

### 3.1 Clean Compile

**All of these must pass:**

```bash
# Clean build from scratch
rm -rf build && mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

- **Zero compiler warnings** (treat warnings as errors)
- **Zero linker errors**
- **All targets build**: `twin_test_runner`, `enginesim`, `libvehicletwin.a`

### 3.2 All Tests Pass

```bash
cd build
ctest --output-on-failure          # All tests pass
./tests/twin_test_runner --gtest_list_tests  # Lists all twin tests
```

**Required pass rate**: 100% of tests in modified components

### 3.3 Sanitizers (Recommended for PRs)

```bash
cmake -GNinja -DCMAKE_BUILD_TYPE=Sanitize ..  # ASAN + UBSAN
ninja && ctest
```

- No memory leaks (ASAN)
- No undefined behavior (UBSAN)
- No data races (TSAN, if enabled)

---

## 4. Documentation Updates

### 4.1 Required Documentation Changes

For each PR, **at least one** of these must be updated if applicable:

| Documentation | When to Update |
|---------------|----------------|
| `INTERFACE_DESIGN.md` | New interface or change to existing interface contract |
| `INTERFACE_CONTRACTS.md` | Any change to pre/postconditions, invariants, error handling |
| `TEST_STRATEGY.md` | New test scenarios, changes to test approach |
| `CLUTCH_PHYSICS_ANALYSIS.md` | Changes to clutch/gearbox physics understanding |
| `TRANSMISSION_RESEARCH.md` | New transmission type, updated shift curves |
| `TELEMETRY_INVENTORY.md` | New signals added/removed |
| Inline code comments | Public APIs, complex algorithms, non-obvious logic |
| `README.md` or user docs | User-facing changes, new features |

### 4.2 API Documentation Standards

**Every public method must have Doxygen-style comment:**

```cpp
/// Set the target RPM for the dyno controller
///
/// \param targetRpm Desired engine RPM (must be in [500, 8000])
/// \pre Controller must be initialized (initialize() returned true)
/// \post Dyno target is updated within 1 ms
/// \throws std::invalid_argument if targetRpm is out of range
/// \note This is the primary RPM control mechanism for dyno-assisted twin
virtual void setTargetRpm(double targetRpm) = 0;
```

### 4.3 Architecture Decision Records (ADRs)

If the PR introduces a **significant architectural change**:
- Create `docs/adr/YYYY-MM-DD-description.md` or add to `ARCHITECTURE_DECISIONS.md`
- Include: context, decision, consequences, alternatives considered
- Link from relevant interface/doc updates

---

## 5. Performance Regression Guard

### 5.1 Performance Budgets

**No PR may cause performance regression beyond these limits:**

| Metric | Budget | How to Measure |
|--------|--------|----------------|
| `ITwinStrategy::update()` latency | ≤ 100 μs | Benchmark with `std::chrono::high_resolution_clock` |
| `ITwinModel::update()` latency | ≤ 5 ms | Same as above |
| Memory allocations per `update()` | 0 (hot path) | Run under ASAN/valgrind or custom allocator |
| `getEngineRpm()` latency | ≤ 100 μs | Benchmark |

### 5.2 Performance Testing

**Include benchmark for performance-critical changes:**

```cpp
// test/bench/StrategyBenchmark.cpp
TEST(StrategyBench, UpdateLatency) {
    auto strategy = createTestStrategy();
    UpstreamSignal signal = createSignal();
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        strategy->update(signal, 3000.0, 0.001);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto avgNs = std::chrono::duration_cast<std::chrono::nanoseconds>
                 (end - start).count() / 10000.0;
    EXPECT_LT(avgNs, 100000); // 100 μs in ns
}
```

**Run before/after PR**:
```bash
./build/tests/strategy_bench --gtest_filter=*Latency
```

### 5.3 Profiling

For changes to hot paths, include `perf` or `vtune` results if available:

```bash
perf stat -e cycles,instructions,cache-misses ./build/tests/twin_test_runner --gtest_filter=*Integration
```

---

## 6. Fail-Fast Verification

### 6.1 Error Handling Review

**Checklist for modified code:**

- [ ] All **I/O errors** (telemetry, file, network) use error codes (return `bool` or set error struct) — **no exceptions**
- [ ] All **logic errors** (invalid arguments, null pointers, invariant violations) **throw exceptions** immediately
- [ ] No `catch (...)` that swallows exceptions without re-throwing or logging
- [ ] No empty `catch` blocks
- [ ] No "disguising" failures (e.g., returning `0` RPM when sensor fails — must throw or return error code)
- [ ] All public methods validate inputs (or document preconditions clearly)
- [ ] Error paths are tested (tests that trigger error conditions)

### 6.2 Example Review

**❌ BAD — Swallows error:**
```cpp
try {
    telemetry.read(signal);
} catch (...) {
    // Silent failure! Twin continues with stale data.
    return;
}
```

**❌ BAD — Disguises failure:**
```cpp
if (!telemetry.read(signal)) {
    signal.rpm = 0;  // Hides the failure — caller thinks RPM is 0
    return;
}
```

**✅ GOOD — Fail-fast:**
```cpp
if (!telemetry.read(signal)) {
    throw TwinModelError("Telemetry read failed: " + 
                         telemetry.getLastError().message);
}
// Or if error code contract:
// return TwinModelResult::Error(telemetry.getLastError());
```

**✅ GOOD — Exception for programming errors:**
```cpp
void setGear(int gear) {
    if (gear < -2 || gear > 6) {
        throw std::invalid_argument("Invalid gear: " + std::to_string(gear));
    }
    // ...
}
```

---

## 7. Commit Message Standards

### 7.1 Format

```
<type>(<scope>): <subject>

<body>

<footer>

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

**Types**:
- `feat` — new feature
- `fix` — bug fix
- `refac` — refactoring (no behavior change)
- `test` — adding/changing tests
- `docs` — documentation only
- `ci` — CI/CD changes
- `perf` — performance improvement
- `chore` — maintenance, build, tooling

**Scope** (optional): `twin`, `strategy`, `gearbox`, `clutch`, `bridge`, `tests`, `ci`, `docs`

**Subject**: ≤ 70 chars, imperative mood ("Add feature" not "Added feature")

**Body**: Explain **what** and **why** (not **how** — that's in the code). Reference issues.

**Footer**: 
- `Closes #123` — closes GitHub issue
- `Co-Authored-By` — required for all commits

### 7.2 Examples

**Good**:
```
feat(twin): add DynoAssistedStrategy for RPM tracking

Implements primary twin strategy using dyno mode for exact RPM
control. Uses ZF 8HP shift curves (A=0.20, B=0.60) for gear
selection. Clutch remains disengaged (pressure=0.0) as per
CLUTCH_PHYSICS_ANALYSIS.md findings.

Includes unit tests for target RPM computation and integration
tests with mock telemetry.

Closes #45
Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

**Bad**:
```
added some stuff for twin

fixed bugs
Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## 8. PR Template Checklist

When creating the PR, the description **must** include:

```markdown
## Summary
Brief description of changes.

## Related Issues
- Closes #XXX
- Blocks #YYY

## Changes Made
- [ ] List of files modified
- [ ] Key architectural decisions
- [ ] Interfaces changed (link to INTERFACE_CONTRACTS.md)

## Testing
- [ ] New unit tests (link to test file)
- [ ] Integration tests pass
- [ ] Performance benchmarks run (no regression)
- [ ] Edge cases covered

## Documentation
- [ ] INTERFACE_DESIGN.md updated (if interfaces changed)
- [ ] INTERFACE_CONTRACTS.md updated (if contracts changed)
- [ ] Test scenarios documented
- [ ] Code comments added for public APIs

## Architecture Review
- [ ] SOLID compliance verified
- [ ] DRY compliance verified
- [ ] Fail-fast error handling verified (no swallowed exceptions)
- [ ] Performance budgets met (≤ 100 μs for update())
- [ ] Coverage ≥ 90% on new code

## Screenshots / Artifacts
- [ ] Test output (if applicable)
- [ ] Coverage report (if applicable)
- [ ] Benchmark results (if applicable)
```

---

## 9. Automatic Quality Gates (CI)

These checks run automatically and **block merge if they fail**:

```yaml
# .github/workflows/quality-gate.yml
- name: Run tests
  run: ctest --output-on-failure
  
- name: Check coverage
  run: ./scripts/check-coverage.sh --min 90
  
- name: Build with warnings as errors
  run: cmake -DCMAKE_CXX_FLAGS="-Werror" .. && ninja
  
- name: Run clang-tidy
  run: run-clang-tidy -checks="*,-llvmlibc-*,-fuchsia-*" -warnings-as-errors="*"
  
- name: Verify no TODO/FIXME in production code
  run: ! git grep -E "TODO|FIXME" -- ':!test/' ':!.claude/'
  
- name: Performance regression check
  run: ./scripts/benchmark.sh --compare-with=HEAD~1
```

---

## 10. Final Approval Checklist (Before Merge)

The PR author and reviewer must confirm:

- [ ] All code compiles with **zero warnings** (`-Werror`)
- [ ] All tests pass (**100%** pass rate)
- [ ] Coverage on new code is **≥ 90%** (verified by CI)
- [ ] No performance regression (**≤ 5%** slowdown in hot paths)
- [ ] Code follows **SOLID** principles (reviewer verified)
- [ ] **DRY** — no duplication (reviewer verified)
- [ ] Fail-fast principle — **no swallowed exceptions**
- [ ] Error contracts documented in `INTERFACE_CONTRACTS.md` (if interfaces changed)
- [ ] Commit messages follow format with `Co-Authored-By`
- [ ] No merge commits (rebase onto `master` first)
- [ ] Target branch is `master` (or `develop` for WIP)

**When in doubt, ask for a second review.**

---

## Summary

This checklist ensures every PR maintains the project's high standards: clean architecture (SOLID/DRY), comprehensive testing (TDD with RED/GREEN/REFACTOR), fail-fast error handling, and performance safety. Treat it as the gatekeeper for code quality.

**Remember**: A merge is a contract with future developers. Make it count.

---