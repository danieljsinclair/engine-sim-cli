.PHONY: all ios-lib ios-lib-sim ios xcode clean help

# Default target
all: ios

# Stage 1a: Build static library for iOS device (arm64)
ios-lib:
	@echo "--- Building iOS device static library (arm64) ---"
	@if [ ! -d build-ios ]; then mkdir -p build-ios; fi
	@cd build-ios && cmake ../.. \
		-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake \
		-DPLATFORM=OS64 \
		-DDEPLOYMENT_TARGET=16.0 \
		-DENABLE_ARC=OFF \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF
	@$(MAKE) -C build-ios engine-sim-bridge
	@libtool -static -o build-ios/libenginesim.a \
		build-ios/engine-sim-bridge/libenginesim.a \
		build-ios/engine-sim/libengine-sim.a \
		build-ios/engine-sim/dependencies/submodules/simple-2d-constraint-solver/libsimple-2d-constraint-solver.a \
		build-ios/engine-sim/dependencies/submodules/csv-io/libcsv-io.a
	@echo "Device static library: build-ios/libenginesim.a"

# Stage 1b: Build static library for iOS Simulator (arm64 + x86_64)
ios-lib-sim:
	@echo "--- Building iOS Simulator static library ---"
	@if [ ! -d build-ios-sim ]; then mkdir -p build-ios-sim; fi
	@cd build-ios-sim && cmake ../.. \
		-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake \
		-DPLATFORM=SIMULATOR64 \
		-DDEPLOYMENT_TARGET=16.0 \
		-DENABLE_ARC=OFF \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF
	@$(MAKE) -C build-ios-sim engine-sim-bridge
	@libtool -static -o build-ios-sim/libenginesim.a \
		build-ios-sim/engine-sim-bridge/libenginesim.a \
		build-ios-sim/engine-sim/libengine-sim.a \
		build-ios-sim/engine-sim/dependencies/submodules/simple-2d-constraint-solver/libsimple-2d-constraint-solver.a \
		build-ios-sim/engine-sim/dependencies/submodules/csv-io/libcsv-io.a
	@echo "Simulator static library: build-ios-sim/libenginesim.a"

# Stage 1c: Create fat binary (device arm64 + simulator x86_64/arm64) for Xcode
# Note: Both device and simulator contain arm64, so we extract unique slices
ios-fat: ios-lib ios-lib-sim
	@echo "--- Creating fat static library ---"
	lipo build-ios-sim/libenginesim.a -thin x86_64 -output build-ios-sim/libenginesim-x86_64.a
	lipo -create build-ios/libenginesim.a build-ios-sim/libenginesim-x86_64.a \
		-output build-ios/libenginesim-fat.a
	rm -f build-ios-sim/libenginesim-x86_64.a
	cp build-ios/libenginesim-fat.a build-ios/libenginesim.a
	@echo "Fat static library: build-ios/libenginesim.a"
	@lipo -info build-ios/libenginesim.a

# Stage 2: Build iOS app for simulator (Xcode links against static library)
ios: ios-lib-sim
	@echo "--- Building iOS app (Simulator) ---"
	@mkdir -p build-ios
	@cp build-ios-sim/libenginesim.a build-ios/libenginesim.a
	@set -o pipefail && xcodebuild \
		-project EngineSimApp/EngineSimApp.xcodeproj \
		-target EngineSimApp \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPhone 16' \
		build 2>&1 | tail -20

# Build and open in Xcode (builds fat binary for device + simulator deploy)
xcode: ios-fat
	@echo "Launching Xcode..."
	@open EngineSimApp/EngineSimApp.xcodeproj

# Clean all iOS build artifacts
clean:
	rm -rf build-ios build-ios-sim
	rm -rf EngineSimApp/build
	rm -rf ~/Library/Developer/Xcode/DerivedData/EngineSimApp-*

# Show help
help:
	@echo "Available targets:"
	@echo "  all          - Build iOS app for simulator (default)"
	@echo "  ios-lib      - Build static library for iOS device (arm64)"
	@echo "  ios-lib-sim  - Build static library for iOS Simulator"
	@echo "  ios-fat      - Build fat binary (device + simulator)"
	@echo "  ios          - Build iOS app for Simulator"
	@echo "  xcode        - Build fat binary and open in Xcode (for device deploy)"
	@echo "  clean        - Clean all iOS build artifacts"
	@echo "  help         - Show this help message"
