.PHONY: all clean rebuild configure submodules

all: submodules configure
	@$(MAKE) -C build

submodules:
	@if [ ! -f "engine-sim-bridge/engine-sim/CMakeLists.txt" ]; then \
		echo "Initializing submodules..."; \
		git submodule update --init --recursive; \
	fi

configure:
	@if [ ! -d build ]; then \
		echo "Creating build directory and running cmake..."; \
		mkdir -p build && cd build && cmake ..; \
	fi

clean:
	@if [ -d build ]; then $(MAKE) -C build clean; fi

rebuild: clean all
