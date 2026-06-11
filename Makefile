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
BUILD_TYPE ?= Release
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

# Build system: Ninja (recommended) or Make (fallback)
# Ninja handles parallel dependency ordering correctly; Make has a known race
# condition where .o recompilation doesn't always trigger re-link with --parallel.
NINJA_BIN := $(shell command -v ninja 2>/dev/null)
ifdef NINJA_BIN
CMAKE_GENERATOR := -G Ninja
CMAKE_BUILD_PARALLEL_FLAG := $(if $(strip $(BUILD_PARALLEL_LEVEL)),-j $(BUILD_PARALLEL_LEVEL),)
$(info [build] Ninja detected — parallel builds enabled)
else
CMAKE_GENERATOR :=
CMAKE_BUILD_PARALLEL_FLAG :=
$(warning [build] Ninja not found — parallel builds disabled to avoid re-link race condition. Install ninja to enable.)
endif

ESP32_DIR ?= engine-sim-esp32
ESP32_PORT ?= $(shell ls /dev/cu.usbserial-* 2>/dev/null | head -1)
# Auto-detect EIM-installed ESP-IDF: prefers latest versioned install, falls back to IDF_PATH env var
IDF_PATH ?= $(or $(wildcard $(HOME)/.espressif/v6.*/esp-idf),$(HOME)/esp/esp-idf)
IDF_ACTIVATE ?= $(firstword $(wildcard $(HOME)/.espressif/tools/activate_idf_*.sh))

.DEFAULT_GOAL := all
.PHONY: all build clean scrub test test-fast test-quick testquick submodules check-cmake check-platform check-submodule remove-orphans \
        force-rebuild sync-es copy-es-mr copy-es-json presets bridge-presets bridge-build \
        run run-json help build-cross clean-cross sonar-clean
.PHONY: esp32 deploy_esp32 run_esp32 clean_esp32

# ============================================================================
# all: Full pipeline -- build + test (default target)
# ============================================================================
all: build test

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
		+$(MAKE) -C engine-sim-bridge scrub; \
		mkdir -p $(BUILD_DIR); \
		echo "$$CURRENT_SUBMODULE" > $(SUBMODULE_STAMP); \
	fi

check-cmake:
	@if [ -f CMakeCache.txt ] && ! grep -q "CMAKE_BUILD_TYPE:STRING=Release" CMakeCache.txt; then \
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

clean: remove-orphans clean_esp32 sonar-clean
	+@$(MAKE) -C engine-sim-bridge clean 2>/dev/null || true
	@if [ -d $(BUILD_DIR) ]; then \
		cmake --build $(BUILD_DIR) --target clean >/dev/null 2>&1 || true; \
	fi
	@rm -rf $(CLI_ES)
	@rm -rf .scannerwork

scrub: clean
	@echo "Scrubbing all build artifacts..."
	+@$(MAKE) -C engine-sim-bridge scrub 2>/dev/null || true
	@rm -rf $(BUILD_DIR) $(CLI_ES)
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

# Explicit long-running tier: bridge golden-audio regressions.
test-deep: build
	@echo "=== [engine-sim-cli] Deep mode: bridge preset golden-audio regressions ==="
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
	@echo "  make sonar-scan - Run SonarQube scan (only re-runs when build/inputs change)"
	@echo "  make test     - Build then run all tests (full)"
	@echo "  make test-deep - Run bridge preset golden-audio regressions"
	@echo "  make test-fast - Build then run fast tests (skips 6 heavy groups)"
	@echo "  make test-quick/testquick - Build then run quick tests only"
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
# SonarQube scan - standalone quality gate for CLI code
# ============================================================================

sonar-scan: $(SONAR_STAMP)

$(SONAR_STAMP): $(COMPILE_DB) $(SONAR_PROJECT_PROPERTIES)
	@echo "=== [engine-sim-cli] Running Sonar scan ==="
	-SONAR_TOKEN="$(or $(SONAR_TOKEN_ES),$(SONAR_TOKEN))" sonar-scanner; touch $@

$(COMPILE_DB): $(BUILD_DIR)/CMakeCache.txt

sonar-clean:
	@rm -f $(SONAR_STAMP)
	@rm -rf .scannerwork

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
