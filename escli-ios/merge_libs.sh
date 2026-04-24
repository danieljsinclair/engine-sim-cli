#!/bin/bash
# Merge static libraries for iOS build
# Handles duplicate object filenames by extracting to temp dir first
set -e

BRIDGE_LIB="$1"
ENGINE_SIM_LIB="$2"
CONSTRAINT_SOLVER_LIB="$3"
CSV_IO_LIB="$4"
OUTPUT_LIB="$5"

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Extract all object files
ar -x "$BRIDGE_LIB" || true
for lib in "$ENGINE_SIM_LIB" "$CONSTRAINT_SOLVER_LIB" "$CSV_IO_LIB"; do
    ar -x "$lib" || true
done

# Create merged archive
ar -rcs "$OUTPUT_LIB" *.o
