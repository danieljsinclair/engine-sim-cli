# Task 4: .mr Script Survey — Transmission & Vehicle Diversity

## 1. ALL Transmissions Are Manual
Every single .mr script models a manual transmission. Zero automatic transmission support anywhere — not in .mr scripts, not in objects.mr, not in C++. `transmission()` node in objects.mr (line 614-617) accepts only `max_clutch_torque`. Gear changes triggered exclusively by keyboard input in `engine_sim_application.cpp:885`. `Transmission` C++ class has `changeGear()` and `setClutchPressure()` but no automatic shift logic.

## 2. Transmission Catalog (all manual)
- **6 gears** is the norm for cars (2JZ, GM LS, Ferrari F136, LFA, Subaru, Audi, all V6s)
- **5 gears**: Honda VTEC (Integra), Honda TRX520 (ATV)
- **1 gear** (direct drive): Kohler CH750, Radial engines, Merlin V12
- **F1**: 6 gears, close-ratio (2.8 to 1.19, 2.35:1 spread)
- **Wide-ratio**: Subaru/Impreza (3.636 to 0.756, 4.8:1 spread) — also reused by all generic V6s
- **Overdrive top gears**: Corvette (0.57), Impreza (0.756), Ferrari (0.80)
- **max_clutch_torque**: 200-500 lb_ft for cars, 35 for small engines, 2000 for aircraft
- **No scripts with more than 6 gears exist**

## 3. Vehicle Catalog
| Script | Vehicle | Mass | Drag | Diff | Tire |
|--------|---------|------|------|------|------|
| 03_2jz.mr | Supra MK4 | 1542 kg | 0.4 | 3.15 | 10" |
| 07_gm_ls.mr | Corvette C5 | 1614 kg | 0.3 | 3.42 | 10" |
| 08_ferrari_f136.mr | "Mustang" (mislabel) | 1614 kg | 0.3 | 3.42 | 10" |
| 10_lfa_v10.mr | LFA | 1614 kg | 0.3 | 3.42 | 10" |
| 06_subaru_ej25.mr | Impreza | 1225 kg | 0.2 | 3.9 | 10" |
| 05_honda_vtec.mr | Integra Type R | 1089 kg | 0.2 | 3.55 | 10" |
| 04_hayabusa.mr | Hayabusa | 255 kg | 0.1 | 2.353 | 8.5" |
| 03_harley_davidson.mr | Harley | 408 kg | 0.1 | 2.0 | 11" |
| 07_audi_i5.mr | Audi Quattro | 1290 kg | 0.3 | 3.55 | 10" |
| 12_ferrari_412_t2.mr | F1 car | 798 kg | 0.9 | 4.10 | 9" |

No SUVs or trucks with vehicle definitions exist. chev_truck_454.mr has no vehicle node. Corvette/Ferrari F136/LFA share identical vehicle definitions (clearly placeholder). Heaviest vehicle is Corvette/LFA at 1614 kg — Tesla Model Y at ~2000 kg would be heaviest by far.

## 4. Engine Characteristics
- **Redlines range**: 3600 rpm (Kohler) to 18000 rpm (F1 V12)
- **Typical cars**: 5500-7000 rpm
- **High-revving**: 8000-9500 rpm (Ferrari F136 9000, LFA 9000, BMW M52B28 7000, Honda VTEC 8400)
- **For AMG C63 comparison**: redline ~7200 rpm
- **Flywheel masses**: 5-30 lb for cars, 50-200 lb for aircraft
- **No turbo/supercharged engines modelled** — all naturally aspirated
- **No Mercedes/AMG engines exist** in the library

## 5. objects.mr transmission() Node
```mr
public node transmission => __engine_sim__transmission {
    input max_clutch_torque [float]: 1000 * units.lb_ft;
    alias output __out [transmission_channel];
}
```
Only `max_clutch_torque` is supported. No shift type, torque converter, shift curves, or shift timing. C++ `Transmission` uses friction constraint model with clutch pressure (0-1). `changeGear()` does energy-conserving RPM recalculation.

## 6. Gap Analysis for Automatic Transmission
1. **No automatic shift logic** anywhere (scripts, objects.mr, or C++)
2. **No torque converter model** — only friction clutch
3. **No shift curve data** — no concept of shift points
4. **No 8+ speed transmissions** — all are 5-6 speed
5. **No AMG/Mercedes engines** or SUV vehicle models
6. **No turbo modeling** at all

## 7. Recommended Tesla Comparison Vehicles
1. **GM LS (07_gm_ls.mr)** — Best template. V8, 1614 kg (closest to Model Y), complete with 6-speed
2. **Ferrari F136 (08_ferrari_f136.mr)** — Same vehicle data, high-revving for sportier feel
3. **2JZ (03_2jz.mr)** — I6, 1542 kg, broad torque character
4. **BMW M52B28 (bmw/M52B28.mr)** — BMW I6 but INCOMPLETE: no vehicle or transmission definition

## 8. Critical Insight: Automatic Transmission Can Be Bridge-Only
The automatic transmission simulation likely does NOT need engine-sim modifications. Since the bridge controls RPM via the simulation loop, it can:
1. Run engine-sim in **dyno mode** (no vehicle/transmission needed at all)
2. Map EV motor RPM/torque to target ICE RPM
3. Apply shift logic entirely in the bridge, changing the target RPM curve at shift points
4. Engine-sim just responds to RPM/throttle commands, which it already does

The .mr scripts provide the sound signature; the bridge provides the driving behavior mapping. This keeps engine-sim untouched and puts all automatic transmission intelligence in the bridge layer.

## 9. Realistic Shift Point Data
- **AMG C63 (performance sedan)**: light throttle 2500-3000 rpm, normal 3000-3500, sport 5500-6500, WOT 6500-7200
- **SUV/Crossover**: light 2000-2500, normal 2500-3000, sport 4500-5500, WOT 5500-6500
- **Sports car**: light 2000-2500, normal 2500-3000, sport 5000-6000, WOT 6500-7500
- Modern 8-10 speed automatics shift every ~800-1000 rpm at WOT to stay in power band
