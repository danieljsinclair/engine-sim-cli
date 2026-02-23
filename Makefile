# Makefile wrapper for engine-sim-cli
# Handles both fresh clone and development builds

BUILD_DIR ?= build

.PHONY: all clean test

all: $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE)

$(BUILD_DIR)/Makefile:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ..

clean:
	@rm -rf $(BUILD_DIR)

test: $(BUILD_DIR)/Makefile
	@cd $(BUILD_DIR) && $(MAKE) test

# Forward other targets
%:
	@if [ ! -f $(BUILD_DIR)/Makefile ]; then \
		mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake ..; \
	fi
	@cd $(BUILD_DIR) && $(MAKE) $@
