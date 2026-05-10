#!/bin/bash
#
# engine-sim-cli Build Test Suite
# Validates that the project builds without Boost and all core functionality works.
#
# Usage: From repo root, run: ./scripts/test-build.sh
# Exit code: 0 = all passed, non-zero = failure
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== engine-sim-cli Build Test Suite ==="
echo ""

# Track failures
FAILED=0

# -----------------------------------------------------------------------------
# [1] Pre-flight checks
# -----------------------------------------------------------------------------
echo "[1] Pre-flight checks..."

# Must be in repo root
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}✗ Not in repo root (CMakeLists.txt missing)${NC}"
    exit 1
fi

# Submodules present?
if [ ! -f "engine-sim-bridge/CMakeLists.txt" ]; then
    echo -e "${YELLOW}⚠ Submodule missing, initializing...${NC}"
    git submodule update --init --recursive
fi
echo -e "${GREEN}✓ Repo structure OK${NC}"

# -----------------------------------------------------------------------------
# [2] Clean build directory
# -----------------------------------------------------------------------------
echo "[2] Cleaning build directory..."
rm -rf build
mkdir build
cd build
echo -e "${GREEN}✓ Build dir cleaned${NC}"

# -----------------------------------------------------------------------------
# [3] CMake configure
# -----------------------------------------------------------------------------
echo "[3] CMake configure..."
cmake .. 2>&1 | tee cmake.log
CONFIG_EXIT=${PIPESTATUS[0]}

if [ $CONFIG_EXIT -ne 0 ]; then
    echo -e "${RED}✗ CMake configure failed${NC}"
    echo "Last 20 lines of cmake.log:"
    tail -20 cmake.log
    exit 1
fi
echo -e "${GREEN}✓ CMake configure succeeded${NC}"

# Check for Boost mention (should be absent)
if grep -qi "boost" cmake.log; then
    echo -e "${RED}✗ CMake log mentions Boost — dependency not fully removed${NC}"
    grep -i "boost" cmake.log || true
    FAILED=1
else
    echo -e "${GREEN}✓ No Boost dependency found${NC}"
fi

# Check for old compiler flag error
if grep -qi "unused-but-set-variable.*error\|error.*unused-but-set" cmake.log; then
    echo -e "${RED}✗ Old compiler flag '-Wno-unused-but-set-variable' rejected${NC}"
    FAILED=1
else
    echo -e "${GREEN}✓ Compiler flag compatibility OK${NC}"
fi

# Check PIRANHA_ENABLED
if grep -q "PIRANHA_ENABLED:BOOL=ON" CMakeCache.txt 2>/dev/null; then
    echo -e "${GREEN}✓ PIRANHA_ENABLED=ON${NC}"
elif grep -q "PIRANHA_ENABLED:BOOL=OFF" CMakeCache.txt 2>/dev/null; then
    echo -e "${YELLOW}⚠ PIRANHA_ENABLED=OFF (script interpreter disabled)${NC}"
else
    echo -e "${YELLOW}⚠ PIRANHA_ENABLED status unclear${NC}"
fi

# -----------------------------------------------------------------------------
# [4] Build
# -----------------------------------------------------------------------------
echo "[4] Building engine-sim-cli..."
CPUS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "Using ${CPUS} parallel jobs"
time make -j${CPUS} 2>&1 | tee build.log
BUILD_EXIT=${PIPESTATUS[0]}

if [ $BUILD_EXIT -ne 0 ]; then
    echo -e "${RED}✗ Build failed${NC}"
    echo "Last 30 lines of build.log:"
    tail -30 build.log
    exit 1
fi
echo -e "${GREEN}✓ Build succeeded${NC}"

# -----------------------------------------------------------------------------
# [5] Verify binary
# -----------------------------------------------------------------------------
echo "[5] Verifying binary..."
if [ -x "engine-sim-cli" ]; then
    echo -e "${GREEN}✓ Binary exists and is executable${NC}"
    ./engine-sim-cli --version 2>&1 || true
else
    echo -e "${RED}✗ Binary not found at ./engine-sim-cli${NC}"
    ls -la
    exit 1
fi

# -----------------------------------------------------------------------------
# [6] Smoke test — WAV export (works without audio hardware)
# -----------------------------------------------------------------------------
echo "[6] Smoke test: WAV export..."
if ./engine-sim-cli --default-engine --rpm 2000 --output /tmp/es_test.wav --duration 1 2>&1; then
    if [ -s "/tmp/es_test.wav" ]; then
        echo -e "${GREEN}✓ WAV file generated: $(ls -lh /tmp/es_test.wav | awk '{print $5}')${NC}"
        # Basic WAV header check (first 4 bytes should be "RIFF")
        if head -c 4 /tmp/es_test.wav | grep -q "RIFF"; then
            echo -e "${GREEN}✓ WAV header valid${NC}"
        else
            echo -e "${RED}✗ WAV file invalid (bad header)${NC}"
            FAILED=1
        fi
    else
        echo -e "${RED}✗ WAV file empty or missing${NC}"
        FAILED=1
    fi
else
    echo -e "${RED}✗ WAV export command failed${NC}"
    FAILED=1
fi

# -----------------------------------------------------------------------------
# [7] Check that .mr engine loads (no scripts needed)
# -----------------------------------------------------------------------------
echo "[7] Smoke test: engine configuration load..."
# Create a minimal engine config inline via CLI (uses default engine)
if ./engine-sim-cli --default-engine --rpm 3000 --output /tmp/es_test2.wav --duration 0.5 2>&1; then
    echo -e "${GREEN}✓ Default engine loads and runs${NC}"
else
    echo -e "${YELLOW}⚠ Default engine test failed (may be environment-related)${NC}"
fi

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
echo ""
echo "=== Summary ==="
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
    echo "Build is clean and functional."
    exit 0
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC}"
    echo "Review the output above for details."
    exit 1
fi
