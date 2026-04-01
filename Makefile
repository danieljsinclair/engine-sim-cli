# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds
#
# IMPORTANT: Always use 'make' from project root, never run 'cmake' directly.
# Running 'cmake -S . -B .' will overwrite this Makefile and break the build.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
SUBMODULE_STAMP = $(BUILD_DIR)/.submodule-stamp

.PHONY: all clean scrub test submodules check-cmake remove-orphans force-rebuild

all: check-cmake submodules check-submodule $(BUILD_DIR)/Makefile
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
clean: remove-orphans
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
	@cd $(BUILD_DIR) && $(MAKE) test ARGS="-V --output-on-failure" 2>&1 | tee $(BUILD_DIR)/test.log
