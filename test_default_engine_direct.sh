#!/bin/bash
# Test running the default engine directly to see if it crashes

echo "Testing default engine with --silent flag..."
timeout 5s ./build/engine-sim-cli --default-engine --duration 0.5 --silent
EXIT_CODE=$?
echo "Exit code: $EXIT_CODE"

if [ $EXIT_CODE -eq 139 ]; then
    echo "SEGFAULT detected"
elif [ $EXIT_CODE -eq 124 ]; then
    echo "Timeout (this is OK)"
else
    echo "Normal exit"
fi
