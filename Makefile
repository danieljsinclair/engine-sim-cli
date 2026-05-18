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
SUBMODULE_STAMP = $(BUILD_DIR)/.submodule-stamp

# Default to parallel build using available CPU cores
MAKEFLAGS += -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: all clean scrub test submodules check-cmake check-platform remove-orphans \
        force-rebuild sync-es copy-es-mr copy-es-json presets bridge-presets \
        run run-json help

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
	@cd $(BUILD_DIR) && $(MAKE)

# Build presets in bridge (depends on check-platform so compiler exists)
bridge-presets: check-platform
	@$(MAKE) -C engine-sim-bridge presets

# ---------------------------------------------------------------------------
# es/ convenience copy — rebuilt from bridge canonical source
# ---------------------------------------------------------------------------
BRIDGE_ES := engine-sim-bridge/es
BRIDGE_PRESET := engine-sim-bridge/preset
CLI_ES := es

copy-es-mr:
	@echo "Syncing es/ .mr files from bridge..."
	@rsync -a --delete --exclude='.git' $(BRIDGE_ES)/ $(CLI_ES)/

copy-es-json: copy-es-mr
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
check-submodule:
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

$(BUILD_DIR)/Makefile: submodules
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
	@cd $(BUILD_DIR) && $(MAKE) engine-sim-cli smoke_tests bridge_unit_tests preset_engine_tests
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
	@echo "  make test     - Build and run all tests"
	@echo "  make presets  - Compile .mr wrappers to JSON presets"
	@echo "  make clean    - Clean build artifacts (fast rebuild)"
	@echo "  make scrub    - Remove entire build directory (full clean)"
	@echo "  make run      - Build and run CLI with .mr script"
	@echo "  make run-json - Build and run CLI with JSON preset"
	@echo "  make help     - Show this help"
