#!/bin/bash
#
# engine-sim-cli Bootstrap
# One-command setup for a fresh clone.
#
# Usage: ./scripts/bootstrap.sh
# Installs dependencies and builds the project.
#

set -e

echo "=== engine-sim-cli Bootstrap ==="
echo ""

# Check OS
OS="$(uname -s)"
if [ "$OS" != "Darwin" ]; then
    echo "ERROR: Only macOS is supported."
    exit 1
fi

# Check for Homebrew
if ! command -v brew &>/dev/null; then
    echo "Homebrew not found. Install from https://brew.sh"
    exit 1
fi

# Install dependencies
echo "[1] Installing dependencies via Homebrew..."
brew install cmake bison flex

# Build
echo "[2] Building..."
make

echo ""
echo "=== Bootstrap complete ==="
echo "Run: ./build/engine-sim-cli --help"
