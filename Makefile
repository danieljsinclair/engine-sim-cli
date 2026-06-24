# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds
#
# IMPORTANT: Always use 'make' from project root, never run 'cmake' directly.
# Running 'cmake -S . -B .' will overwrite this Makefile and break the build.
#
# Build cascade: make build -> cmake -> compile -> presets -> sync-es
# Test cascade:  make test -> build -> bridge tests + CLI tests
# Full pipeline: make all -> build + test

BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
BUILD_PHASE0_SPIKES ?= OFF
# Set to 1 to allow Debug builds (needed for coverage instrumentation).
ALLOW_DEBUG_BUILD ?= 0
CTEST_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_PARALLEL_LEVEL ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
CTEST_VERBOSE ?= 0
CTEST_UI_FLAGS := $(if $(filter 1,$(CTEST_VERBOSE)),-V,--progress)
SUBMODULE_STAMP = $(BUILD_DIR)/.submodule-stamp
SONAR_STAMP := $(BUILD_DIR)/.sonar-scan.stamp
SONAR_PROJECT_PROPERTIES := sonar-project.properties
COMPILE_DB := $(BUILD_DIR)/compile_commands.json

# Separate build-cov directory for coverage/sonar. RelWithDebInfo + llvm-cov
# instrumentation. Has its own CMakeCache so coverage reconfigure does NOT
# invalidate the test build in $(BUILD_DIR). Mirrors the bridge's model.
BUILD_COV_DIR ?= build-cov
BUILD_TYPE_COV ?= RelWithDebInfo
SONAR_PROJECT_PROPERTIES := sonar-project.properties
COMPILE_DB := $(BUILD_COV_DIR)/compile_commands.json
COVERAGE_REPORT := $(BUILD_COV_DIR)/coverage.txt
# SonarCloud generic coverage XML (lcov -> XML via the bridge's
# scripts/lcov_to_xml.py). Read by the scanner via sonar.coverageReportPaths.
COVERAGE_XML := $(BUILD_COV_DIR)/coverage-sonar.xml
SONAR_REPORT := $(BUILD_COV_DIR)/sonar-report.json
# Cached SonarCloud measures (coverage headline) + REMOVED-facet (issues whose
# source was deleted). Both curled by sonar-summary so build_summary.py reads
# the SAME numbers sonar_summary.py shows -- total = open + removed (OPEN union
# REMOVED, matching the dashboard severity widget). Mirrors the bridge.
SONAR_MEASURES := $(BUILD_COV_DIR)/sonar-measures.json
SONAR_REMOVED_FACET := $(BUILD_COV_DIR)/sonar-removed-facet.json
BUILD_STAMP := $(BUILD_DIR)/.build-ready.stamp
BUILD_COV_STAMP := $(BUILD_COV_DIR)/.build-cov-ready.stamp

# Bridge's instrumented coverage archive. The CLI build-cov configures with
# -DBRIDGE_BUILD_DIR pointing here, so the CLI links the bridge's INSTRUMENTED
# build-cov libenginesim.a — coverage then attributes bridge source lines.
BRIDGE_DIR := engine-sim-bridge
BRIDGE_BUILD_COV_LIB := $(BRIDGE_DIR)/build-cov/libenginesim.a

# LLVM coverage tools: Xcode toolchain first, plain which() fallback.
LLVM_COV := $(shell xcrun --find llvm-cov 2>/dev/null || which llvm-cov 2>/dev/null)
LLVM_PROFDATA := $(shell xcrun --find llvm-profdata 2>/dev/null || which llvm-profdata 2>/dev/null)

# Source inputs that affect the coverage build. Invalidation on change.
BUILD_INPUTS := $(shell find Makefile CMakeLists.txt src include test tools engine-sim -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.cmake' \) 2>/dev/null | sort)

# Build system: Ninja (recommended) or Make (fallback)
# Ninja handles parallel dependency ordering correctly; Make has a known race
# condition where .o recompilation doesn't always trigger re-link with --parallel.
NINJA_BIN := $(shell command -v ninja 2>/dev/null)
ifdef NINJA_BIN
CMAKE_GENERATOR := -G Ninja
CMAKE_BUILD_PARALLEL_FLAG := $(if $(strip $(BUILD_PARALLEL_LEVEL)),-j $(BUILD_PARALLEL_LEVEL),)
# Ninja banner: shown only on real builds, NEVER during `summary` (which recurses
# into this Makefile via $(MAKE) -C engine-sim-cli summary and would otherwise
# interleave a "[build] Ninja detected" line into the end-of-make headline block).
# SUMMARY_QUIET is exported by the summary targets so the recursion stays silent.
ifndef SUMMARY_QUIET
$(info [build] Ninja detected — parallel builds enabled)
endif
else
CMAKE_GENERATOR :=
CMAKE_BUILD_PARALLEL_FLAG :=
ifndef SUMMARY_QUIET
$(warning [build] Ninja not found — parallel builds disabled to avoid re-link race condition. Install ninja to enable.)
endif
endif

ESP32_DIR ?= engine-sim-esp32
ESP32_PORT ?= $(shell ls /dev/cu.usbserial-* 2>/dev/null | head -1)
# Auto-detect EIM-installed ESP-IDF: prefers latest versioned install, falls back to IDF_PATH env var
IDF_PATH ?= $(or $(wildcard $(HOME)/.espressif/v6.*/esp-idf),$(HOME)/esp/esp-idf)
IDF_ACTIVATE ?= $(firstword $(wildcard $(HOME)/.espressif/tools/activate_idf_*.sh))

.DEFAULT_GOAL := all
.PHONY: all build clean scrub test test-fast test-quick testquick submodules check-cmake check-platform check-submodule remove-orphans \
        force-rebuild sync-es copy-es-mr copy-es-json presets bridge-presets bridge-build \
        run run-json help build-cross clean-cross sonar-scan sonar-clean sonar-summary \
        coverage-run coverage-clean coverage-summary summary
.PHONY: esp32 deploy_esp32 run_esp32 clean_esp32

# ============================================================================
# all: Full pipeline -- build + test (default target). `summary` is the LAST
# step so the end-of-make headline (russian doll: cli line, then bridge line)
# is the final build output.
# ============================================================================
all: build test summary

# ============================================================================
# build: Compile everything -- configure, compile, build presets, sync es/
#
# Ordering is sequential via dependencies:
#   1. submodules        -- init recursive submodules
#   2. $(BUILD_DIR)/CMakeCache.txt -- cmake configure
#   3. check-platform    -- verify macOS, then compile everything
#   4. bridge-presets    -- compile .mr -> .json (needs preset_compiler from step 3)
#   5. sync-es           -- copy bridge es/ + preset/ -> CLI es/
# ============================================================================
build: check-platform bridge-presets sync-es

check-platform: check-cmake bridge-build $(BUILD_DIR)/CMakeCache.txt
	@if [ "$$(uname)" != "Darwin" ]; then \
		echo ""; \
		echo "ERROR: engine-sim-cli only supports macOS (CoreAudio/AudioUnit)."; \
		echo "       Linux and Windows are not supported — no audio hardware provider exists."; \
		echo "       Planned next platforms: ESP32, Android."; \
		echo "       See README.md for the platform support roadmap."; \
		echo ""; \
		exit 1; \
	fi
	+@cmake --build $(BUILD_DIR) $(CMAKE_BUILD_PARALLEL_FLAG)

# Bridge builds independently -- must complete before CLI cmake configure
bridge-build: submodules check-submodule
	+@$(MAKE) -C engine-sim-bridge build ALLOW_DEBUG_BUILD=$(ALLOW_DEBUG_BUILD)

# Build presets in bridge (depends on check-platform so compiler exists)
bridge-presets: check-platform
	+@$(MAKE) -C engine-sim-bridge presets

# ---------------------------------------------------------------------------
# es/ convenience copy -- full mirror from bridge
#
# The bridge/es/ directory is the source of truth for all .mr scripts and
# supporting files. The bridge Makefile's CANDIDATE_ENGINES controls which
# get compiled to JSON. This target just mirrors the full directory.
# ---------------------------------------------------------------------------
BRIDGE_ES := engine-sim-bridge/es
BRIDGE_PRESET := engine-sim-bridge/preset
CLI_ES := es

copy-es-mr:
	@echo "Syncing es/ from bridge..."
	@mkdir -p $(CLI_ES)
	@rsync -a --exclude='.git' $(BRIDGE_ES)/ $(CLI_ES)/

copy-es-json: bridge-presets
	@if [ -d $(BRIDGE_PRESET) ] && ls $(BRIDGE_PRESET)/*.json >/dev/null 2>&1; then \
		echo "Syncing JSON presets..."; \
		cp $(BRIDGE_PRESET)/*.json $(CLI_ES)/; \
	else \
		echo "No presets built yet -- run 'make presets' first."; \
	fi

sync-es: copy-es-mr copy-es-json

presets: bridge-presets

# ---------------------------------------------------------------------------
# Submodule and CMake configuration
# ---------------------------------------------------------------------------
check-submodule: submodules
	@CURRENT_SUBMODULE=$$(git submodule status engine-sim-bridge | awk '{print $$1}'); \
	STAMPED_SUBMODULE=$$(cat $(SUBMODULE_STAMP) 2>/dev/null); \
	if [ "$$CURRENT_SUBMODULE" != "$$STAMPED_SUBMODULE" ]; then \
		echo "Submodule changed, forcing rebuild..."; \
		rm -f $(BUILD_DIR)/CMakeCache.txt; \
		$(MAKE) -C engine-sim-bridge scrub; \
		mkdir -p $(BUILD_DIR); \
		echo "$$CURRENT_SUBMODULE" > $(SUBMODULE_STAMP); \
	fi

check-cmake:
	@if [ -f CMakeCache.txt ] && ! grep -qE "CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)" CMakeCache.txt; then \
		echo "ERROR: Root CMakeCache.txt exists with wrong BUILD_TYPE. This happens when running 'cmake -S . -B .' directly."; \
		echo "Please run 'git checkout Makefile' and then use 'make' instead."; \
		exit 1; \
	fi

submodules:
	@if [ ! -f engine-sim-bridge/CMakeLists.txt ]; then \
		echo "Initializing submodules..."; \
		git submodule update --init --recursive; \
	fi

$(BUILD_DIR)/CMakeCache.txt: check-submodule
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUILD_PHASE0_SPIKES=$(BUILD_PHASE0_SPIKES) \
		-DCMAKE_SUPPRESS_DEVELOPER_WARNINGS=ON \
		-DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
		..

# ---------------------------------------------------------------------------
# Clean targets -- cascade to bridge
# ---------------------------------------------------------------------------
remove-orphans:
	@rm -rf assets
	@rm -f *.dylib libenginesim*.dylib
	@find . -name "*.dylib*" -type l -delete 2>/dev/null || true
	@rm -f $(SUBMODULE_STAMP)
	@find . -path ./$(BUILD_DIR) -prune -o -name "CMakeCache.txt" -type f -print -delete 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "CMakeFiles" -type d -print -exec rm -rf {} + 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "cmake_install.cmake" -type f -print -delete 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "CTestTestfile.cmake" -type f -print -delete 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "*_include.cmake" -type f -print -delete 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "*.a" -type f -print -delete 2>/dev/null || true
	@find . -path ./$(BUILD_DIR) -prune -o -name "_deps" -type d -print -exec rm -rf {} + 2>/dev/null || true

clean: remove-orphans clean_esp32 sonar-clean coverage-clean
	+@$(MAKE) -C engine-sim-bridge clean 2>/dev/null || true
	@if [ -d $(BUILD_DIR) ]; then \
		cmake --build $(BUILD_DIR) --target clean >/dev/null 2>&1 || true; \
	fi
	@rm -rf $(CLI_ES)
	@rm -rf .scannerwork

scrub: clean
	@echo "Scrubbing all build artifacts..."
	+@$(MAKE) -C engine-sim-bridge scrub 2>/dev/null || true
	@rm -rf $(BUILD_DIR) $(BUILD_COV_DIR) $(CLI_ES)
	@$(MAKE) remove-orphans
	@rm -rf .scannerwork
	@echo "Build artifacts scrubbed. Run 'make' to rebuild."

# ---------------------------------------------------------------------------
# Test -- build first, then run all test suites via CTest
# ---------------------------------------------------------------------------
test: build
	+@total_start=$$(date +%s); \
	bridge_elapsed=0; \
	cli_elapsed=0; \
	echo "=== [engine-sim-cli] Stage 1/2: bridge tests ==="; \
	bridge_start=$$(date +%s); \
	if $(MAKE) -C engine-sim-bridge test; then \
		bridge_end=$$(date +%s); \
		bridge_elapsed=$$((bridge_end - bridge_start)); \
		echo "=== [engine-sim-cli] Stage 1/2: bridge tests PASSED ($${bridge_elapsed}s) ==="; \
	else \
		bridge_end=$$(date +%s); \
		bridge_elapsed=$$((bridge_end - bridge_start)); \
		echo "=== [engine-sim-cli] Stage 1/2: bridge tests FAILED ($${bridge_elapsed}s) ==="; \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=SKIPPED total=$${total_elapsed}s ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi; \
	echo "=== [engine-sim-cli] Stage 2/2: cli/unit/integration tests ==="; \
	cli_start=$$(date +%s); \
	if (cd $(BUILD_DIR) && ctest $(CTEST_UI_FLAGS) --output-on-failure --output-log ../test.log -j$(CTEST_JOBS)); then \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: PASS (full) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: ALL TESTS PASSED ===\033[0m\n'; \
		$(MAKE) sonar-scan coverage-summary sonar-summary || \
			echo "=== [engine-sim-cli] sonar/coverage summary skipped (non-fatal) ==="; \
	else \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (full) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

# Fast test path: build + bridge fast tests + full CLI tests.
test-fast: build
	+@total_start=$$(date +%s); \
	bridge_elapsed=0; \
	cli_elapsed=0; \
	echo "=== [engine-sim-cli] Fast mode: bridge test-fast + full cli/unit/integration ==="; \
	bridge_start=$$(date +%s); \
	if $(MAKE) -C engine-sim-bridge test-fast; then \
		bridge_end=$$(date +%s); \
		bridge_elapsed=$$((bridge_end - bridge_start)); \
		echo "=== [engine-sim-cli] Bridge fast suite PASSED ($${bridge_elapsed}s) ==="; \
	else \
		bridge_end=$$(date +%s); \
		bridge_elapsed=$$((bridge_end - bridge_start)); \
		echo "=== [engine-sim-cli] Bridge fast suite FAILED ($${bridge_elapsed}s) ==="; \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge-fast=$${bridge_elapsed}s cli=SKIPPED total=$${total_elapsed}s ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi; \
	cli_start=$$(date +%s); \
	if (cd $(BUILD_DIR) && ctest $(CTEST_UI_FLAGS) --output-on-failure --output-log ../test.log -j$(CTEST_JOBS)); then \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge-fast=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: PASS (fast) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: FAST TESTS PASSED ===\033[0m\n'; \
	else \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge-fast=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (fast) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

# Quick mode: minimal bridge-only infra checks for tight edit/run loops.
test-quick: build
	@echo "=== [engine-sim-cli] Quick mode: bridge test-quick only ==="
	+@if $(MAKE) -C engine-sim-bridge test-quick; then \
		echo "=== [engine-sim-cli] SUMMARY: PASS (quick) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: QUICK TESTS PASSED ===\033[0m\n'; \
	else \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (quick) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

testquick: test-quick

# Explicit long-running tier: bridge isomorphism + engine-sim physics tests.
test-deep: build
	@echo "=== [engine-sim-cli] Deep mode: bridge isomorphism + engine-sim physics tests ==="
	+@if $(MAKE) -C engine-sim-bridge test-deep; then \
		echo "=== [engine-sim-cli] SUMMARY: PASS (deep) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: DEEP TESTS PASSED ===\033[0m\n'; \
	else \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (deep) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

testdeep: test-deep

# ---------------------------------------------------------------------------
# Convenience targets
# ---------------------------------------------------------------------------
run: build
	./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr

run-json: build
	./build/engine-sim-cli --interactive --play --script es/v8_gm_ls.json

help:
	@echo "engine-sim-cli Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build + test (complete pipeline)"
	@echo "  make build    - Compile everything (no tests)"
	@echo "  make sonar-scan - Run SonarQube scan with coverage (only re-runs when build/inputs change)"
	@echo "  make coverage-run - Run CLI tests with coverage instrumentation"
	@echo "  make coverage-summary - Show local coverage % from lcov.info"
	@echo "  make sonar-summary - Show SonarCloud issues summary"
	@echo "  make test     - Build then run all tests (full)"
	@echo "  make test-deep - Run bridge isomorphism + engine-sim physics tests"
	@echo "  make test-fast - Build then run fast tests (skips 6 heavy groups)"
	@echo "  make test-quick/testquick - Build then run quick tests only"
	@echo "  make summary  - Print the end-of-make headline (cli line, then bridge line)"
	@echo "  make presets  - Compile .mr wrappers to JSON presets"
	@echo "  make clean    - Clean build artifacts (fast rebuild)"
	@echo "  make scrub    - Remove entire build directory (full clean)"
	@echo "  make run      - Build and run CLI with .mr script"
	@echo "  make run-json - Build and run CLI with JSON preset"
	@echo "  make esp32    - Build ESP32 firmware"
	@echo "  make deploy_esp32 - Flash ESP32 firmware"
	@echo "  make run_esp32    - Build, flash, and monitor ESP32"
	@echo "  make help     - Show this help"

# ============================================================================
# SonarQube scan + LLVM coverage -- mirrors the bridge's honed model.
#
#   build      : RelWithDebInfo, tests run via `make test` (separate dir).
#   build-cov  : RelWithDebInfo + llvm-cov instrumentation, for coverage/sonar.
#
# Coverage runs BEFORE sonar (fresh coverage report). Tests run BEFORE sonar
# (fail-fast). coverage.txt and sonar-report.json are the make targets
# (artefacts, not stamps) -- re-run only on input changes (second make: nothing
# to do). Drops the `|| true` that swallowed test failures. The CLI build-cov
# links the bridge's INSTRUMENTED build-cov archive
# (-DBRIDGE_BUILD_DIR=engine-sim-bridge/build-cov) so coverage attributes bridge
# source lines. DRY: reuses the bridge's run_coverage_tests.sh (recursive glob
# finds CLI build-cov/test/* binaries), sonar_summary.py, coverage_summary.py.
# ============================================================================

# Bridge instrumented lib. The bridge instruments its ``build/`` directory
# (Debug + llvm-cov), not a separate build-cov. The CLI therefore points
# -DBRIDGE_BUILD_DIR at the bridge's instrumented ``build/`` directly. This
# rule ensures the instrumented lib is current by re-running the bridge's
# coverage-run (cheap when build/ is already instrumented).
BRIDGE_COV_DIR := $(BRIDGE_DIR)/build
$(BRIDGE_BUILD_COV_LIB): $(BRIDGE_COV_DIR)/libenginesim.a
	@:

$(BRIDGE_COV_DIR)/libenginesim.a:
	+@$(MAKE) -C $(BRIDGE_DIR) coverage-run
	@test -e $@ || { echo "ERROR: bridge instrumented lib missing at $@"; exit 1; }

# Coverage build: separate build-cov dir with llvm-cov instrumentation.
# RelWithDebInfo (NOT Debug) + -fprofile-instr-generate/-fcoverage-mapping/-g.
# Points at the bridge's instrumented build/ (the bridge instruments build/,
# not build-cov) so coverage attributes bridge source. Depends on the bridge
# instrumented lib existing first (ordering).
$(BUILD_COV_DIR)/CMakeCache.txt: CMakeLists.txt $(BRIDGE_BUILD_COV_LIB)
	@mkdir -p $(BUILD_COV_DIR)
	@cd $(BUILD_COV_DIR) && cmake \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE_COV) \
		-DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
		-DBUILD_PHASE0_SPIKES=$(BUILD_PHASE0_SPIKES) \
		-DCMAKE_SUPPRESS_DEVELOPER_WARNINGS=ON \
		-DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
		-DBRIDGE_BUILD_DIR=$(CURDIR)/$(BRIDGE_COV_DIR) \
		..

$(BUILD_COV_STAMP): $(BUILD_INPUTS) $(BUILD_COV_DIR)/CMakeCache.txt
	@echo "=== [engine-sim-cli] Building coverage (build-cov, RelWithDebInfo+instr) ==="
	@cmake --build $(BUILD_COV_DIR) $(CMAKE_BUILD_PARALLEL_FLAG)
	@touch $@

# coverage-run: run CLI tests on the coverage-instrumented build, merge profdata,
# export coverage.txt (llvm-cov text) + lcov.info. File-artefact target: re-runs
# only when build-cov, source inputs, or the coverage script change. The bridge's
# run_coverage_tests.sh uses a recursive glob that finds CLI build-cov/test/*
# binaries (Ninja layout) and writes coverage.txt + lcov.info itself. No `|| true`
# -- the script collects profraw from every binary then surfaces failures at the
# end (honesty gate), so a failing test propagates non-zero.
$(COVERAGE_REPORT): $(BUILD_COV_STAMP) $(BUILD_INPUTS) $(BRIDGE_DIR)/scripts/run_coverage_tests.sh
	@LLVM_PROFDATA="$(LLVM_PROFDATA)" LLVM_COV="$(LLVM_COV)" \
		bash $(BRIDGE_DIR)/scripts/run_coverage_tests.sh $(BUILD_COV_DIR)

coverage-run: $(COVERAGE_REPORT)

sonar-scan: $(SONAR_REPORT)

# SONAR_REPORT depends on the coverage ARTEFACT (coverage.txt), so coverage is
# regenerated (tests re-run) before the scan reads the fresh coverage report.
# Re-scans only when coverage/compile-db/properties/sources change. The curl
# writes SONAR_REPORT itself. NOTE: this sonar-scanner rejects -q, so log output
# is redirected to a file and tailed on failure (the bridge's pattern).
$(SONAR_REPORT): $(COVERAGE_REPORT) $(COMPILE_DB) $(SONAR_PROJECT_PROPERTIES) $(BUILD_INPUTS)
	@if [ -z "$${SONAR_TOKEN_ES}" ] && [ -z "$${SONAR_TOKEN}" ]; then \
		echo "ERROR: Neither SONAR_TOKEN_ES nor SONAR_TOKEN is set. Run: source ~/.zshrc"; \
		exit 1; \
	fi
	@echo "=== [engine-sim-cli] Running Sonar scan ==="
	@SONAR_TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}" sonar-scanner > $(BUILD_COV_DIR)/sonar-scanner.log 2>&1; \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "=== [engine-sim-cli] sonar-scanner failed (rc=$$rc); see $(BUILD_COV_DIR)/sonar-scanner.log ==="; \
			tail -n 20 $(BUILD_COV_DIR)/sonar-scanner.log; \
			exit $$rc; \
		fi
	@echo "=== [engine-sim-cli] Caching SonarCloud issue report ==="
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
	curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_engine-sim-cli&ps=500&statuses=OPEN" \
		> $(SONAR_REPORT) 2>/dev/null || true
	@echo "=== [engine-sim-cli] Caching SonarCloud measures (lines_to_cover) ==="
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
	curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/measures/component?component=danieljsinclair_engine-sim-cli&metricKeys=lines_to_cover,uncovered_lines" \
		> $(BUILD_COV_DIR)/sonar-measures.json 2>/dev/null || true

$(COMPILE_DB): $(BUILD_COV_DIR)/CMakeCache.txt

coverage-clean:
	@rm -f $(COVERAGE_REPORT) $(COVERAGE_XML) $(SONAR_REPORT) $(BUILD_COV_STAMP) $(BUILD_COV_DIR)/coverage.profdata $(BUILD_COV_DIR)/lcov.info $(BUILD_COV_DIR)/profraw/*.profraw
	@rm -rf $(BUILD_COV_DIR)/profraw

sonar-clean:
	@rm -f $(SONAR_REPORT)
	@rm -rf .scannerwork

# Sonar summary -- display issues from a LIVE SonarCloud report (DRY: bridge script).
# No prereq on $(SONAR_REPORT): this must NEVER trigger a scan. Curls the API
# live at display time (refreshing the cached file) so local counts always
# match the dashboard. If no token, prints a hint.
# SHOW_TYPE_SEVERITY=1 also shows the legacy severity (CRITICAL/MAJOR/MINOR/INFO).
SHOW_TYPE_SEVERITY ?= 0
sonar-summary:
	@echo ""
	@echo "=== [engine-sim-cli] BEGIN: SonarCloud issues summary ==="
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
	if [ -z "$$TOKEN" ]; then echo "  No token"; exit 0; fi; \
	curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_engine-sim-cli&ps=500&statuses=OPEN&facets=impactSeverities" > $(SONAR_REPORT) 2>/dev/null || true; \
	curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_engine-sim-cli&ps=1&resolutions=REMOVED&facets=impactSeverities" > $(SONAR_REMOVED_FACET) 2>/dev/null || true; \
	curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/measures/component?component=danieljsinclair_engine-sim-cli&metricKeys=coverage,lines_to_cover,uncovered_lines" > $(SONAR_MEASURES) 2>/dev/null || true; \
	python3 engine-sim-bridge/scripts/sonar_summary.py $(SONAR_REPORT) $(if $(filter 1,$(SHOW_TYPE_SEVERITY)),--type-severity,) --label engine-sim-cli --removed-facet $(SONAR_REMOVED_FACET)
	@echo "=== [engine-sim-cli] END: SonarCloud issues summary ==="

# Coverage summary -- emit the shared multi-line coverage block (SonarCloud-live
# headline + local lcov + exclusions). DRY: delegates to the bridge's
# scripts/coverage_block.py (shared block-emission helper; contains NONE of the
# bridge's top-5/dead-stripped/ESP32 extras). The local lcov is the honest
# llvm-cov number over /src/ from build-cov/lcov.info. No prereq: this must
# NEVER trigger a scan. Graceful fallback if no token / fetch fails / no local
# file (shows what's available, never crashes).
coverage-summary:
	@echo ""
	@echo "=== [engine-sim-cli] BEGIN: coverage summary ==="
	@python3 engine-sim-bridge/scripts/coverage_block.py \
		--project-key danieljsinclair_engine-sim-cli \
		--local-cov $(BUILD_COV_DIR)/lcov.info \
		--local-type lcov \
		--exclusions $(SONAR_PROJECT_PROPERTIES) \
		--label "[engine-sim-cli]"
	@echo "=== [engine-sim-cli] END: coverage summary ==="

# summary: the end-of-make HEADLINE (russian doll). Prints the CLI's OWN line
# first (tests from the teed test.log, coverage from the cached sonar-measures
# JSON -- the same headline coverage_block.py shows, sonar from the cached
# sonar-report.json), then recurses into the bridge so its line follows. Order
# is SELF-then-submodule so the nesting reads top-down (cli, then bridge).
# Greps plain numbers + re-emits coloured -- no live re-query, never triggers a
# scan/test, never crashes; missing fields are omitted gracefully.
BUILD_SUMMARY_SCRIPT := engine-sim-bridge/scripts/build_summary.py
summary:
	@python3 $(BUILD_SUMMARY_SCRIPT) \
		--label "[engine-sim-cli]" \
		--test-log test.log \
		--cov-measures $(SONAR_MEASURES) \
		--local-cov $(BUILD_COV_DIR)/lcov.info --local-type lcov \
		--sonar-report $(SONAR_REPORT) \
		--removed-facet $(SONAR_REMOVED_FACET)
	+@$(MAKE) -C engine-sim-bridge summary SUMMARY_QUIET=1

# ---------------------------------------------------------------------------
# Cross-compilation (caller sets PLATFORM, e.g. OS64, SIMULATOR64)
# Not iOS-aware -- just accepts a platform variable for the CMake toolchain.
# ---------------------------------------------------------------------------
ifdef PLATFORM
CROSS_BUILD_DIR := build-$(PLATFORM)
CROSS_TOOLCHAIN := ios.toolchain.cmake
endif

build-cross: submodules
ifndef PLATFORM
	$(error PLATFORM is required. Usage: make build-cross PLATFORM=OS64)
endif
	@mkdir -p $(CROSS_BUILD_DIR)
	@cd $(CROSS_BUILD_DIR) && cmake \
		-DCMAKE_TOOLCHAIN_FILE=$(CROSS_TOOLCHAIN) \
		-DPLATFORM=$(PLATFORM) \
		-DDEPLOYMENT_TARGET=16.0 \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF \
		-DBRIDGE_USE_PREBUILT=OFF \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) ..
	@$(MAKE) -C $(CROSS_BUILD_DIR) engine-sim-cli -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

clean-cross:
ifdef PLATFORM
	@rm -rf $(CROSS_BUILD_DIR)
else
	@rm -rf build-OS64 build-SIMULATOR64
endif

# -----------------------------------------------------------------------------
# ESP32 targets (ESP32-S3 + MAX98357A I2S DAC)
# Prerequisites: ESP-IDF installed via EIM
#   brew tap espressif/eim && brew install eim && eim install
# Hardware: GPIO 4=BCLK, GPIO 5=LRCLK, GPIO 6=DIN -> MAX98357A
# -----------------------------------------------------------------------------

# Resolve activation: source EIM activation script or legacy export.sh
# idf.py invocation: EIM uses shell aliases (invisible to make's non-interactive shells),
# so we call it directly via the venv python instead.
IDF_VENV_PYTHON := $(shell ls $(HOME)/.espressif/tools/python/v*/venv/bin/python 2>/dev/null | head -1)
IDF_PYTHON ?= $(or $(IDF_VENV_PYTHON),python3)
IDF_PY ?= $(IDF_PYTHON) $(IDF_PATH)/tools/idf.py
define ESP32_ACTIVATE
if [ -f "$(IDF_ACTIVATE)" ]; then . "$(IDF_ACTIVATE)" 2>/dev/null; elif [ -f "$(IDF_PATH)/export.sh" ]; then . "$(IDF_PATH)/export.sh" 2>/dev/null; fi
endef

esp32: submodules
	@if [ ! -d "$(IDF_PATH)" ] && [ -z "$(IDF_ACTIVATE)" ]; then \
		echo ""; \
		echo "ERROR: ESP-IDF not found."; \
		echo "  Install via EIM: brew tap espressif/eim && brew install eim && eim install"; \
		echo "  Or set IDF_PATH manually: make esp32 IDF_PATH=/path/to/esp-idf"; \
		echo ""; \
		exit 1; \
	fi
	@if [ ! -d $(ESP32_DIR) ]; then \
		echo "ERROR: ESP32 project not found at $(ESP32_DIR)/"; \
		exit 1; \
	fi
	@$(ESP32_ACTIVATE) && cd $(ESP32_DIR) && $(IDF_PY) build

deploy_esp32: esp32
	@if [ -z "$(ESP32_PORT)" ]; then \
		echo "ERROR: No ESP32 serial port found."; \
		echo "Connect ESP32 via USB and check /dev/cu.usbserial-*"; \
		echo "Or set ESP32_PORT manually: make deploy_esp32 ESP32_PORT=/dev/cu.usbserial-XXXX"; \
		exit 1; \
	fi
	@$(ESP32_ACTIVATE) && cd $(ESP32_DIR) && $(IDF_PY) -p $(ESP32_PORT) flash

run_esp32: deploy_esp32
	@$(ESP32_ACTIVATE) && cd $(ESP32_DIR) && $(IDF_PY) -p $(ESP32_PORT) monitor

clean_esp32:
	@if [ -d $(ESP32_DIR) ]; then \
		rm -rf $(ESP32_DIR)/build; \
	fi
