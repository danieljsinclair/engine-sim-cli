#!/bin/bash
# Update all submodules to canonical master after history rewrite
# This script is idempotent - safe to run multiple times
#
# In other folders with orphaned submodules, run this sequence:
#   1. git fetch --all
#   2. git pull
#   3. git submodule update --init --recursive
#   4. ./tools/update-submodules-after-rewrite.sh
#
# Or if you prefer a fresh approach:
#   1. make clean  (if available)
#   2. make        (rebuilds and syncs submodules)
#
# Usage: ./tools/update-submodules-after-rewrite.sh

set -e

# Get the root of the repo
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# First, sync the parent repo and all submodules
echo "=== PREVIEW MODE ==="
echo ""
echo "1. Syncing parent repo and fetching all submodules..."
echo "   (git fetch --all)"
echo "   (git submodule update --init --recursive)"
echo ""

# Array of submodules that need checking (both top-level and nested)
SUBMODULES=(
    "engine-sim-bridge"
    "engine-sim-bridge/engine-sim"
)

# Also attach top-level submodules to their master branches (removes detached HEAD)
TOP_LEVEL_SUBMODULES=(
    "engine-sim-bridge"
)

echo "2. Checking for orphaned submodule commits..."
echo ""

CHANGES=()

for submodule in "${SUBMODULES[@]}"; do
    if [ -d "$submodule/.git" ] || [ -f "$submodule/.git" ]; then
        cd "$submodule"

        # Show what we're checking
        CURRENT=$(git rev-parse HEAD)
        CURRENT_SHORT=$(git rev-parse --short HEAD)

        # Fetch to get latest remote info (from all remotes for safety)
        git fetch --all > /dev/null 2>&1 || {
            echo "  ⚠ Could not fetch $submodule, skipping"
            cd "$REPO_ROOT"
            continue
        }

        ORIGIN_MASTER=$(git rev-parse origin/master)
        ORIGIN_MASTER_SHORT=$(git rev-parse --short origin/master)

        if [ "$CURRENT" != "$ORIGIN_MASTER" ]; then
            CURRENT_MSG=$(git log -1 --format="%s" $CURRENT 2>/dev/null || echo "unknown")
            ORIGIN_MSG=$(git log -1 --format="%s" $ORIGIN_MASTER 2>/dev/null || echo "unknown")

            echo "  Found: $submodule"
            echo "    Current: $CURRENT_SHORT - $CURRENT_MSG"
            echo "    Will be: $ORIGIN_MASTER_SHORT - $ORIGIN_MSG"
            echo ""

            CHANGES+=("$submodule")
        else
            echo "  ✓ $submodule already at origin/master ($CURRENT_SHORT)"
            echo ""
        fi

        cd "$REPO_ROOT"
    fi
done

# Ask for confirmation
if [ ${#CHANGES[@]} -gt 0 ]; then
    echo "Ready to apply these changes? (y/n)"
    read -r CONFIRM
    if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
        echo "Cancelled."
        exit 0
    fi

    echo ""
    echo "=== APPLYING CHANGES ==="
    echo ""

    for submodule in "${CHANGES[@]}"; do
        echo "Updating $submodule..."
        cd "$REPO_ROOT"
        cd "$submodule"
        # Reset local master to match origin/master
        git fetch --all
        git checkout -B master origin/master
        cd "$REPO_ROOT"
    done

    # Also attach top-level submodules to their master branches (removes detached HEAD)
    for submodule in "${TOP_LEVEL_SUBMODULES[@]}"; do
        echo "Attaching $submodule to master branch..."
        cd "$REPO_ROOT"
        cd "$submodule"
        git fetch --all > /dev/null 2>&1
        git checkout -B master origin/master > /dev/null 2>&1
        cd "$REPO_ROOT"
    done

    echo ""
    echo "Staging and committing parent repo..."
    git add engine-sim-bridge

    if ! git diff --cached --quiet; then
        git commit -m "chore: Update submodules to canonical master after history rewrite"
        echo ""
        echo "✓ Success! Changes committed."
        echo "   Now run: git push"
    else
        echo "No changes to commit"
    fi
else
    echo "✓ All submodules already synced"
fi
