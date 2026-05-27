# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds
#
# IMPORTANT: Always use 'make' from project root, never run 'cmake' directly.
# Running 'cmake -S . -B .' will overwrite this Makefile and break the build.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CTEST_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
SUBMODULE_STAMP = $(BUILD_DIR)/.submodule-stamp

# Default to parallel build using available CPU cores
MAKEFLAGS += -j$(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

ESP32_DIR ?= engine-sim-esp32
ESP32_PORT ?= $(shell ls /dev/cu.usbserial-* 2>/dev/null | head -1)
# Auto-detect EIM-installed ESP-IDF: prefers latest versioned install, falls back to IDF_PATH env var
IDF_PATH ?= $(or $(wildcard $(HOME)/.espressif/v6.*/esp-idf),$(HOME)/esp/esp-idf)
IDF_ACTIVATE ?= $(firstword $(wildcard $(HOME)/.espressif/tools/activate_idf_*.sh))

.PHONY: all clean scrub test submodules check-cmake check-platform remove-orphans force-rebuild
.PHONY: esp32 deploy_esp32 run_esp32 clean_esp32

all: check-platform check-cmake submodules check-submodule $(BUILD_DIR)/Makefile

check-platform:
	@if [ "$$(uname)" != "Darwin" ]; then \
		echo ""; \
		echo "ERROR: engine-sim-cli only supports macOS (CoreAudio/AudioUnit)."; \
		echo "       Linux and Windows are not supported — no audio hardware provider exists."; \
		echo "       Planned next platforms: ESP32, Android."; \
		echo "       See README.md for the platform support roadmap."; \
		echo ""; \
		exit 1; \
	fi
	@cd $(BUILD_DIR) && $(MAKE)

# Check if submodule changed - if so, force rebuild
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

# Remove orphaned binaries and symlinks from root and other unexpected locations
remove-orphans:
	@rm -f *.dylib libenginesim*.dylib
	@find . -name "*.dylib*" -type l -delete 2>/dev/null || true
	@find . -name "CMakeCache.txt" -not -path "./$(BUILD_DIR)/*" -delete 2>/dev/null || true
	@rm -f $(SUBMODULE_STAMP)

# Clean build artifacts (keeps CMakeCache.txt for fast rebuild)
clean: remove-orphans clean_esp32
	@if [ -d $(BUILD_DIR) ]; then \
		$(MAKE) -C $(BUILD_DIR) clean 2>/dev/null || true; \
	fi
	@if [ -d $(BUILD_DIR)/engine-sim-bridge ]; then \
		$(MAKE) -C $(BUILD_DIR)/engine-sim-bridge clean 2>/dev/null || true; \
	fi
	@$(MAKE) -C engine-sim-bridge clean 2>/dev/null || true

# Full clean - removes everything including build directories (superset of clean)
scrub: clean
	@echo "Scrubbing all build artifacts..."
	@$(MAKE) -C engine-sim-bridge scrub 2>/dev/null || true
	@rm -rf $(BUILD_DIR)
	@$(MAKE) remove-orphans
	@echo "Build artifacts scrubbed. Run 'make' to rebuild."

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

test: $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE) engine-sim-cli smoke_tests bridge_unit_tests unit_tests telemetry_isp_tests integration_tests
	@cd $(BUILD_DIR) && $(MAKE) test ARGS="-V --output-on-failure -j$(CTEST_JOBS)" 2>&1 | tee test.log

run: all
	./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr

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