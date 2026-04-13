# Baseline File Management Analysis

**Document Version:** 1.0
**Date:** 2026-04-08
**Status:** IN PROGRESS
**Author:** Documentation Writer

---

## Executive Summary

This document analyzes the current state of baseline file management for audio regression testing and provides recommendations for improvements.

## Current State Analysis

### Baseline Files Status

**✅ POSITIVE FINDINGS:**

1. **Baseline Directory Exists**: `test/unit/baselines/` directory is properly organized
2. **Baseline Files Tracked in Git**: All baseline `.dat` files are tracked in source control
3. **README Documentation Exists**: Comprehensive README.md explains purpose and usage
4. **Test Infrastructure in Place**: AudioRegressionTest.cpp has baseline capture and comparison logic

**Baseline Files Present:**
- `threaded_renderer_baseline.dat` - Normal rendering scenario
- `threaded_renderer_baseline_wrap.dat` - Wrap-around read scenario
- `threaded_renderer_baseline_underrun.dat` - Buffer underrun scenario
- `sync_pull_renderer_baseline.dat` - Normal sync-pull rendering
- `sync_pull_renderer_baseline_wrap.dat` - Sync-pull wrap scenario
- `sync_pull_renderer_baseline_underrun.dat` - Sync-pull underrun scenario

### Issues Identified

**🔴 CRITICAL ISSUE - Build Failure**

**Current Blocker:**
```cpp
/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/ThreadedStrategy.cpp:104:68: error:
no member named 'channels' in 'AudioState'
```

**Root Cause:**
ThreadedStrategy.cpp line 105 attempts to access `context->audioState.channels`:
```cpp
std::vector<float> silence(preFillFrames * context->audioState.channels);
```

However, `AudioState` struct only contains:
- `std::atomic<bool> isPlaying`
- `int sampleRate`

There is NO `channels` member.

**Impact:**
- Build fails, preventing baseline test execution
- Baseline regeneration is blocked
- Regression testing cannot be enabled

**🟡 MEDIUM ISSUE - Test Disabled in CMake**

**CMakeLists.txt Comment:**
```cmake
# AudioRegressionTest.cpp - TEMPORARILY DISABLED due to missing ThreadedRenderer
```

**Reasoning:** This comment is outdated. The issue is not "missing ThreadedRenderer" but rather the compilation error above.

**Impact:**
- Regression tests cannot run
- Baseline comparison functionality is unavailable
- Refactoring safety net is disabled

**🟡 MEDIUM ISSUE - Duplicate Baseline Files**

**Orphaned Baseline Files in Root:**
```
/Users/danielsinclair/vscode/escli.refac7/threaded_renderer_baseline.dat
/Users/danielsinclair/vscode/escli.refac7/sync_pull_renderer_baseline_underrun.dat
/Users/danielsinclair/vscode/escli.refac7/sync_pull_renderer_baseline.dat
```

**Issue:** Baseline files in root directory (not in test/unit/baselines/)

**Impact:**
- Confusion about which baseline files are authoritative
- Potential for stale baselines to persist
- Violation of single source of truth principle

## Requirements Analysis

### Original Requirements from Task Assignment

1. ✅ **Investigate baseline file management approach** - COMPLETED
   - Baseline directory structure is sound
   - Files are tracked in git
   - Documentation exists

2. 🔄 **Consider checking baseline *.dat files into test/unit/baselines/** - IN PROGRESS
   - Files are already tracked in git
   - Need to verify they should be tracked (vs. generated locally)

3. ❌ **Create baseline regeneration script** - NOT STARTED
   - No dedicated script exists
   - Manual process documented in README
   - Should be automated

4. ❌ **Enable regression tests in CMake** - NOT STARTED
   - Currently disabled in CMakeLists.txt
   - Blocked by compilation error

5. ✅ **Document the baseline management process** - PARTIALLY COMPLETED
   - README.md exists with good documentation
   - This document provides additional analysis

## Recommendations

### IMMEDIATE ACTIONS (P0)

1. **Fix Compilation Error in ThreadedStrategy.cpp**
   ```cpp
   // Current (INCORRECT):
   std::vector<float> silence(preFillFrames * context->audioState.channels);

   // Corrected (uses STEREO_CHANNELS constant):
   std::vector<float> silence(preFillFrames * STEREO_CHANNELS);
   ```
   - **Assignee:** @tech-architect
   - **Priority:** CRITICAL - blocks all baseline work
   - **Estimated Time:** 5 minutes

2. **Remove Orphaned Baseline Files**
   ```bash
   rm /Users/danielsinclair/vscode/escli.refac7/threaded_renderer_baseline.dat
   rm /Users/danielsinclair/vscode/escli.refac7/sync_pull_renderer_baseline_underrun.dat
   rm /Users/danielsinclair/vscode/escli.refac7/sync_pull_renderer_baseline.dat
   ```
   - **Assignee:** @doc-writer or @tech-architect
   - **Priority:** HIGH
   - **Estimated Time:** 2 minutes

3. **Enable Regression Tests in CMake**
   ```cmake
   # Remove the comment disabling AudioRegressionTest.cpp
   # AudioRegressionTest.cpp - TEMPORARILY DISABLED due to missing ThreadedRenderer
   # Change to:
   AudioRegressionTest.cpp
   ```
   - **Assignee:** @tech-architect
   - **Priority:** HIGH
   - **Estimated Time:** 2 minutes

### SHORT-TERM ACTIONS (P1)

4. **Create Baseline Regeneration Script**
   ```bash
   #!/bin/bash
   # scripts/regenerate_baselines.sh
   cd build
   ./test/unit/unit_tests --gtest_filter="AudioRegression*.Capture*"

   # Copy generated baselines to source tree
   mkdir -p ../test/unit/baselines
   cp *.dat ../test/unit/baselines/

   echo "Baselines regenerated successfully"
   ```

   - **Assignee:** @tech-architect or @doc-writer
   - **Priority:** HIGH
   - **Estimated Time:** 30 minutes

5. **Update CMakeLists.txt Comment**
   ```cmake
   # Update comment to reflect actual state
   # AudioRegressionTest.cpp - Baseline regression tests for ThreadedStrategy and SyncPullStrategy
   ```

   - **Assignee:** @doc-writer
   - **Priority:** MEDIUM
   - **Estimated Time:** 5 minutes

### LONG-TERM CONSIDERATIONS (P2)

6. **Evaluate Baseline File Tracking Strategy**

**Question:** Should baseline files be tracked in git?

**Pros of Tracking in Git:**
- Ensures all developers use same baselines
- Prevents accidental modification of baseline behavior
- Clear audit trail of baseline changes
- Works well for stable, versioned baselines

**Cons of Tracking in Git:**
- Baselines are binary files (diffs not human-readable)
- Binary files bloat git history
- Baseline updates create "noisy" commits
- Merge conflicts with binary files are difficult

**Alternative Approach:**
- Track baselines in git but use `.gitattributes` to mark as binary
- Document baseline changes clearly in commit messages
- Consider generating baselines from deterministic tests at build time

7. **Baseline Version Management**

**Current:** No versioning system for baselines

**Recommendation:** Consider baseline versioning for major refactors
- Baseline format version in file header
- Compatibility checks for loading old baselines
- Clear upgrade path when baseline format changes

## Implementation Plan

### Phase 1: Critical Fixes (1 day)

**Goal:** Unblock baseline testing and enable regression tests

**Tasks:**
1. Fix ThreadedStrategy.cpp compilation error (channels → STEREO_CHANNELS)
2. Remove orphaned baseline files from root directory
3. Enable AudioRegressionTest.cpp in CMakeLists.txt
4. Verify tests compile and run successfully

**Success Criteria:**
- ✅ Build succeeds without errors
- ✅ Baseline capture tests run successfully
- ✅ Baseline comparison tests run successfully
- ✅ All orphaned baseline files removed

### Phase 2: Automation (2 days)

**Goal:** Streamline baseline regeneration and management

**Tasks:**
1. Create baseline regeneration script (`scripts/regenerate_baselines.sh`)
2. Add script to git and document usage
3. Update README.md with script reference
4. Consider adding baseline management commands to Makefile

**Success Criteria:**
- ✅ Baseline regeneration can be done with single command
- ✅ Script is documented and tested
- ✅ README.md references script
- ✅ Process is repeatable and reliable

### Phase 3: Documentation (1 day)

**Goal:** Ensure baseline management is well-documented and maintainable

**Tasks:**
1. Update CMakeLists.txt comments to reflect actual state
2. Update ARCHITECTURE_TODO.md with baseline management status
3. Document baseline file format specification
4. Create troubleshooting guide for common baseline issues

**Success Criteria:**
- ✅ All documentation is accurate and up-to-date
- ✅ Common issues are documented with solutions
- ✅ Team can maintain baseline system independently

## Testing Strategy

### Baseline Validation

**Pre-Refactoring Validation:**
1. Run baseline capture tests to generate current baselines
2. Verify baselines contain expected data patterns
3. Store baselines in git with clear commit message

**Post-Refactoring Validation:**
1. Run comparison tests against stored baselines
2. Verify byte-for-byte match for all scenarios
3. If mismatch occurs, investigate and fix refactoring
4. Only commit when all comparisons pass

### Test Scenarios Covered

**ThreadedStrategy Baselines:**
1. Normal rendering (no wrap, no underrun)
2. Wrap-around read scenario
3. Buffer underrun scenario

**SyncPullStrategy Baselines:**
1. Normal rendering scenario
2. Wrap-around read scenario
3. Bridge underrun scenario

**Total:** 6 baseline scenarios covering all critical rendering paths

## Success Metrics

### Quantitative Metrics

- **Baseline Coverage:** 6 scenarios (100% of critical paths)
- **Test Pass Rate:** Goal: 100% of comparisons must pass
- **Build Time:** Baseline regeneration < 30 seconds
- **File Size:** Each baseline < 10KB (manageable)

### Qualitative Metrics

- **Clarity:** Baseline purpose and process is clear to all developers
- **Reliability:** Baseline regeneration is deterministic and repeatable
- **Maintainability:** Baseline system is easy to understand and modify
- **Safety:** Regression tests prevent unintended behavior changes

## Conclusion

The baseline file management infrastructure is fundamentally sound with a proper directory structure, git tracking, and documentation. However, there are critical blockers that must be addressed:

1. **CRITICAL:** Fix compilation error in ThreadedStrategy.cpp
2. **HIGH:** Enable regression tests in CMake
3. **HIGH:** Remove orphaned baseline files
4. **MEDIUM:** Create baseline regeneration automation

Once these issues are resolved, the baseline system will provide a robust safety net for audio refactoring work, ensuring that all behavior changes are intentional and verified.

---

*End of Baseline File Management Analysis*
