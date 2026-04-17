.PHONY: all ios-lib ios xcode clean help

# Default target
all: ios

# Stage 1: Build static library for iOS arm64 using CMake
ios-lib:
	@echo "--- Building iOS static library (arm64) ---"
	@if [ ! -d build-ios ]; then mkdir -p build-ios; fi
	@cd build-ios && cmake ../.. \
		-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake \
		-DPLATFORM=OS64 \
		-DDEPLOYMENT_TARGET=16.0 \
		-DENABLE_ARC=OFF \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF
	@$(MAKE) -C build-ios engine-sim-bridge
	@echo "Static library: build-ios/libenginesim.a"

# Stage 2: Build iOS app (Xcode links against static library)
ios: ios-lib
	@echo "--- Building iOS app ---"
	@set -o pipefail && xcodebuild \
		-project EngineSimApp/EngineSimApp.xcodeproj \
		-target EngineSimApp \
		-destination 'platform=iOS Simulator,name=iPhone 16' \
		build 2>&1 | tail -10

# Build and open in Xcode (for device deploy)
xcode: ios
	@echo "Launching Xcode..."
	@open EngineSimApp/EngineSimApp.xcodeproj

# Clean all iOS build artifacts
clean:
	rm -rf build-ios
	rm -rf EngineSimApp/build
	rm -rf ~/Library/Developer/Xcode/DerivedData/EngineSimApp-*

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build iOS app (default)"
	@echo "  ios-lib  - Build static library for iOS arm64"
	@echo "  ios      - Build iOS app (requires ios-lib)"
	@echo "  xcode    - Build iOS app and open in Xcode (for device deploy)"
	@echo "  clean    - Clean all iOS build artifacts"
	@echo "  help     - Show this help message"
