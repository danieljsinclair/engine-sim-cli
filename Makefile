# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds

BUILD_DIR ?= build

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
	@cd $(BUILD_DIR) && cmake ..

clean:
	@rm -rf $(BUILD_DIR)

test: $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE) test
