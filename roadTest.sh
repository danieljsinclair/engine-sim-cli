#!/bin/bash

NAME="${1:-RoadTrip}"
V="../escli.vehicle-sim"
VC="$V/captures"

set -x

(TRIP="${NAME}_$(date +%Y-%m-%d-%H%M%S)" && "$V/build-native/vehicle-sim" \
    --connect usb:/dev/cu.usbserial-210 --vehicle tesla --stdout-csv \
    --log "$V/$TRIP" 2>"$VC/$TRIP.stderr.txt" | \
./build/engine-sim-cli --start --live-telemetry --wheel-coupling pin --play \
    --gearbox-log "$VC/$TRIP.txt" --auto --script es_new/C63_TeslaY.mr)
