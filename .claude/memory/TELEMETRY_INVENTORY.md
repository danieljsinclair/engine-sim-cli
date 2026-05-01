# Tesla Telemetry Signal Inventory

## Signals Currently Extracted by vehicle-sim (from Tesla BLE)

| Signal Name | Source | Raw Units | Converted Units | Rate Hz | In vehicle-sim? | Classification |
|-------------|--------|-----------|-----------------|---------|-----------------|----------------|
| Throttle Position | Tesla BLE | uint8_t (0-100) | percent (0-100) | ~10 (estimated) | Yes | Essential |
| Speed | Tesla BLE | uint16_t (little endian) | km/h | ~10 (estimated) | Yes | Essential |
| Acceleration | Tesla BLE | int8_t (accel*10) | G | ~10 (estimated) | Yes | Nice-to-have |
| Brake Position | Tesla BLE | uint8_t (0-100) | percent (0-100) | ~10 (estimated) | Yes | Nice-to-have |

## Additional Tesla CAN Signals (from DBC research and online resources)

| Signal Name | Source | Raw Units | Converted Units | Rate Hz | In vehicle-sim? | Classification |
|-------------|--------|-----------|-----------------|---------|-----------------|----------------|
| Vehicle Speed | Tesla CAN | TBD (likely uint16) | km/h | 10-100 | No | Essential |
| Throttle Position (Accelerator Pedal) | Tesla CAN | TBD (likely uint8_t or uint16) | percent (0-100) | 10-50 | No | Essential (duplicate of BLE) |
| Brake Pressure | Tesla CAN | TBD (likely uint16) | bar or psi | 10-50 | No | Nice-to-have |
| Battery State of Charge | Tesla CAN | TBD (likely uint8_t) | percent (0-100) | 1-10 | No | Nice-to-have |
| Gear Selector Position | Tesla CAN | TBD (likely enum: P,R,N,D) | discrete | 2-10 | No | Nice-to-have |
| Motor Torque | Tesla CAN | TBD (likely int16) | Nm | 10-100 | No | Nice-to-have |
| Motor RPM | Tesla CAN | TBD (likely uint16) | rpm | 10-100 | No | Red herring |
| Wheel Speed (FL) | Tesla CAN | TBD (likely uint16) | km/h or m/s | 10-100 | No | Nice-to-have |
| Wheel Speed (FR) | Tesla CAN | TBD (likely uint16) | km/h or m/s | 10-100 | No | Nice-to-have |
| Wheel Speed (RL) | Tesla CAN | TBD (likely uint16) | km/h or m/s | 10-100 | No | Nice-to-have |
| Wheel Speed (RR) | Tesla CAN | TBD (likely uint16) | km/h or m/s | 10-100 | No | Nice-to-have |
| Battery Power | Tesla CAN | TBD (likely sint16) | kW | 10-50 | No | Nice-to-have |

## Recommended Minimum Viable Signal Set for Phase 1

For the initial twin integration (Phase 1), the essential signals are:
1. **Speed** (road speed) - essential for kinematic modeling
2. **Throttle Position** (accelerator pedal) - essential for torque request modeling

These two signals are sufficient to begin validating the twin's dynamic response against real vehicle behavior. Additional signals can be incorporated in later phases to enhance fidelity:
- Brake pressure for braking performance tuning
- Battery state of charge for energy consumption modeling
- Gear selector for drive mode awareness
- Motor torque for load validation (though indirect for ICE twin)

Note: Wheel speeds and individual motor RPM are less critical for a simple twin model focused on longitudinal dynamics.