# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds

BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all clean test submodules

all: submodules $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE)

submodules:
	@if [ ! -f engine-sim-bridge/CMakeLists.txt ]; then \
		echo "Initializing submodules..."; \
		git submodule update --init --recursive; \
	fi

$(BUILD_DIR)/Makefile: submodules
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) ..

clean:
	@rm -rf $(BUILD_DIR)

test: $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE) test
