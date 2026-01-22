all:
	mkdir -p build
	cd build && cmake .. && make engine-sim-cli

clean:
	rm -rf build

.PHONY: all clean
