.PHONY: all ios xcode clean scrub help

# Default target: just build both platform libraries
all: build-iphoneos build-iphonesimulator

# Build BOTH platforms, THEN open Xcode GUI
xcode: build-iphoneos build-iphonesimulator
	@echo "Launching Xcode..."
	@open EngineSimApp/EngineSimApp.xcodeproj

# Convenience: build both + open Xcode (same as 'xcode')
ios: build-iphoneos build-iphonesimulator
	@echo "iOS libraries built. Opening Xcode..."
	@open EngineSimApp/EngineSimApp.xcodeproj

build-iphoneos:
	@if [ ! -d build-iphoneos ]; then mkdir -p build-iphoneos; fi
	@cd build-iphoneos && cmake ../.. \
		-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake \
		-DPLATFORM=OS64 \
		-DDEPLOYMENT_TARGET=16.0 \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF > /dev/null
	@$(MAKE) -C build-iphoneos engine-sim-bridge --no-print-directory -j8

build-iphonesimulator:
	@if [ ! -d build-iphonesimulator ]; then mkdir -p build-iphonesimulator; fi
	@cd build-iphonesimulator && cmake ../.. \
		-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake \
		-DPLATFORM=SIMULATOR64 \
		-DDEPLOYMENT_TARGET=16.0 \
		-DBUILD_TESTS=OFF \
		-DBUILD_BRIDGE_TESTS=OFF > /dev/null
	@$(MAKE) -C build-iphonesimulator engine-sim-bridge --no-print-directory -j8

# Clean all iOS build artifacts
clean:
	rm -rf build-iphoneos build-iphonesimulator
	rm -rf EngineSimApp/build
	rm -rf ~/Library/Developer/Xcode/DerivedData/EngineSimApp-*
# Kill any running Xcode and nuke all DerivedData when really broken
scrub: clean
	killall Xcode 2>/dev/null || true
	sleep 1
	rm -rf ~/Library/Developer/Xcode/DerivedData

# Show help
help:
	@echo "Available targets:"
	@echo "  all         - Build static libraries for both device and simulator"
	@echo "  ios         - Build both libraries then open Xcode"
	@echo "  xcode       - Build both libraries then open Xcode (alias for ios)"
	@echo "  clean       - Remove build-iphoneos/ build-iphonesimulator/ and DerivedData"
	@echo "  scrub       - clean + kill Xcode + nuke all DerivedData"
	@echo "  deploy      - Headless CI build (both libs + xcodebuild)"
	@echo "  help        - Show this help message"

# Headless CI build: builds both platforms, no GUI
deploy: build-iphoneos build-iphonesimulator
	@echo "--- Building iOS app headless for CI ---"
	@set -o pipefail && xcodebuild \n		-project EngineSimApp/EngineSimApp.xcodeproj \n		-target EngineSimApp \n		-configuration Release \n		-destination 'generic/platform=iOS' \n		clean build 2>&1 | tail -20
	@echo "✅ Build completed successfully. Output in EngineSimApp/build/"
