# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds
#
# IMPORTANT: Always use 'make' from project root, never run 'cmake' directly.
# Running 'cmake -S . -B .' will overwrite this Makefile and break the build.
#
# Build cascade: make → cmake → compile → presets → sync-es
# Test cascade:  make test → CLI smoke tests + bridge unit tests + preset tests

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CTEST_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
CTEST_VERBOSE ?= 0
SUBMODULE_STAMP = $(BUILD_DIR)/.submodule-stamp

# Default to parallel build using available CPU cores
MAKEFLAGS += -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: all clean scrub test test-fast test-quick testquick submodules check-cmake check-platform check-submodule remove-orphans \
        force-rebuild sync-es copy-es-mr copy-es-json presets bridge-presets \
        run run-json help build-cross clean-cross

# ============================================================================
# all: Full build cascade — configure, compile, build presets, sync es/
#
# Ordering is sequential via dependencies:
#   1. submodules        — init recursive submodules
#   2. $(BUILD_DIR)/Makefile — cmake configure
#   3. check-platform    — verify macOS, then compile everything
#   4. bridge-presets    — compile .mr → .json (needs preset_compiler from step 3)
#   5. sync-es           — copy bridge es/ + preset/ → CLI es/
# ============================================================================
all: check-platform bridge-presets sync-es

check-platform: check-cmake submodules check-submodule $(BUILD_DIR)/Makefile
	@if [ "$$(uname)" != "Darwin" ]; then \
		echo ""; \
		echo "ERROR: engine-sim-cli only supports macOS (CoreAudio/AudioUnit)."; \
		exit 1; \
	fi
	@cd $(BUILD_DIR) && $(MAKE)

# Build presets in bridge (depends on check-platform so compiler exists)
bridge-presets: check-platform
	@$(MAKE) -C engine-sim-bridge presets

# ---------------------------------------------------------------------------
# es/ convenience copy — full mirror from bridge
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
		echo "No presets built yet — run 'make presets' first."; \
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
		rm -f $(BUILD_DIR)/engine-sim-bridge/libenginesim*.dylib; \
		rm -f $(BUILD_DIR)/libenginesim*.dylib; \
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

$(BUILD_DIR)/Makefile: check-submodule
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) ..

# ---------------------------------------------------------------------------
# Clean targets — cascade to bridge
# ---------------------------------------------------------------------------
remove-orphans:
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

clean: remove-orphans
	@$(MAKE) -C engine-sim-bridge clean 2>/dev/null || true
	@if [ -d $(BUILD_DIR) ]; then \
		$(MAKE) -C $(BUILD_DIR) clean 2>/dev/null || true; \
	fi
	@rm -rf $(CLI_ES)

scrub: clean
	@echo "Scrubbing all build artifacts..."
	@$(MAKE) -C engine-sim-bridge scrub 2>/dev/null || true
	@rm -rf $(BUILD_DIR) $(CLI_ES)
	@$(MAKE) remove-orphans
	@echo "Build artifacts scrubbed. Run 'make' to rebuild."

# ---------------------------------------------------------------------------
# Test — runs all test suites via CTest
# ---------------------------------------------------------------------------
test: $(BUILD_DIR)/Makefile
	@total_start=$$(date +%s); \
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
	if (cd $(BUILD_DIR) && $(MAKE) engine-sim-cli smoke_tests bridge_unit_tests unit_tests telemetry_isp_tests integration_tests preset_engine_tests); then \
		:; \
	else \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] Stage 2/2: cli/unit/integration build FAILED ($${cli_elapsed}s) ==="; \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi; \
	if (cd $(BUILD_DIR) && $(MAKE) test ARGS="$(if $(filter 1,$(CTEST_VERBOSE)),-V,) --output-on-failure --output-log ../test.log -j$(CTEST_JOBS)"); then \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		echo "=== [engine-sim-cli] Stage 2/2: cli/unit/integration tests PASSED ($${cli_elapsed}s) ==="; \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: PASS (full) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: ALL TESTS PASSED ===\033[0m\n'; \
	else \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		echo "=== [engine-sim-cli] Stage 2/2: cli/unit/integration tests FAILED ($${cli_elapsed}s) ==="; \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] TIME: bridge=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (full) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

# Fast test path: keep default `make test` full coverage, but allow
# developer inner-loop skips for known heavy bridge groups.
test-fast: $(BUILD_DIR)/Makefile
	@total_start=$$(date +%s); \
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
	if (cd $(BUILD_DIR) && $(MAKE) engine-sim-cli smoke_tests bridge_unit_tests unit_tests telemetry_isp_tests integration_tests preset_engine_tests); then \
		:; \
	else \
		cli_end=$$(date +%s); \
		cli_elapsed=$$((cli_end - cli_start)); \
		total_end=$$(date +%s); \
		total_elapsed=$$((total_end - total_start)); \
		echo "=== [engine-sim-cli] Fast mode cli/unit/integration build FAILED ($${cli_elapsed}s) ==="; \
		echo "=== [engine-sim-cli] TIME: bridge-fast=$${bridge_elapsed}s cli=$${cli_elapsed}s total=$${total_elapsed}s ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi; \
	if (cd $(BUILD_DIR) && $(MAKE) test ARGS="$(if $(filter 1,$(CTEST_VERBOSE)),-V,) --output-on-failure --output-log ../test.log -j$(CTEST_JOBS)"); then \
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
test-quick: $(BUILD_DIR)/Makefile
	@echo "=== [engine-sim-cli] Quick mode: bridge test-quick only ==="
	@if $(MAKE) -C engine-sim-bridge test-quick; then \
		echo "=== [engine-sim-cli] SUMMARY: PASS (quick) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: QUICK TESTS PASSED ===\033[0m\n'; \
	else \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (quick) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

testquick: test-quick

# Explicit long-running tier: bridge golden-audio regressions.
test-deep: $(BUILD_DIR)/Makefile
	@echo "=== [engine-sim-cli] Deep mode: bridge preset golden-audio regressions ==="
	@if $(MAKE) -C engine-sim-bridge test-deep; then \
		echo "=== [engine-sim-cli] SUMMARY: PASS (deep) ==="; \
		printf '\033[0;32m=== [engine-sim-cli] RESULT: DEEP TESTS PASSED ===\033[0m\n'; \
	else \
		echo "=== [engine-sim-cli] SUMMARY: FAIL (deep) ==="; \
		printf '\033[0;31m=== [engine-sim-cli] RESULT: TESTS FAILED ===\033[0m\n'; \
		exit 1; \
	fi

testdeep: test-deep
	@cd $(BUILD_DIR) && $(MAKE) engine-sim-cli smoke_tests bridge_unit_tests preset_engine_tests preset_isomorphism_tests
	@cd $(BUILD_DIR) && ctest -V --output-on-failure -j$(CTEST_JOBS) 2>&1 | tee ../test.log

# ---------------------------------------------------------------------------
# Convenience targets
# ---------------------------------------------------------------------------
run: all
	./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr

run-json: all
	./build/engine-sim-cli --interactive --play --script es/v8_gm_ls.json

help:
	@echo "engine-sim-cli Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build everything (configure → compile → presets → sync)"
	@echo "  make test      - Build and run all tests (full)"
	@echo "  make test-deep - Run bridge preset golden-audio regressions"
	@echo "  make test-fast - Run fast mode (skips 6 heavy bridge groups)"
	@echo "  make test-quick/testquick - Run bridge quick-only checks"
	@echo "  make presets   - Compile .mr wrappers to JSON presets"
	@echo "  make clean    - Clean build artifacts (fast rebuild)"
	@echo "  make scrub    - Remove entire build directory (full clean)"
	@echo "  make run      - Build and run CLI with .mr script"
	@echo "  make run-json - Build and run CLI with JSON preset"
	@echo "  make help     - Show this help"

# ---------------------------------------------------------------------------
# Cross-compilation (caller sets PLATFORM, e.g. OS64, SIMULATOR64)
# Not iOS-aware — just accepts a platform variable for the CMake toolchain.
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
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) ..
	@$(MAKE) -C $(CROSS_BUILD_DIR) engine-sim-bridge -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

clean-cross:
ifdef PLATFORM
	@rm -rf $(CROSS_BUILD_DIR)
else
	@rm -rf build-OS64 build-SIMULATOR64
endif
