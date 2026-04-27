# Clutch Constraint Physics Analysis

## Exact Answer
At max clutch pressure (m_clutchPressure = 1.0), the constraint behaves as **slip-allowing** because the constraint enforces v_theta(crankshaft) == v_theta(vehicleMass) only when the required torque to maintain lock does not exceed m_maxClutchTorque. When the required torque exceeds this limit, slip occurs.

## Detailed Analysis

### Clutch Constraint Formulation
From `/Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/dependencies/submodules/simple-2d-constraint-solver/src/clutch_constraint.cpp`:

- **Jacobian Matrix (lines 24-30)**:
  ```
  output->J[0][0] = 0.0;
  output->J[0][1] = 0.0;
  output->J[0][2] = -1.0;  // body1 (crankshaft) angular velocity
  output->J[0][3] = 0.0;
  output->J[0][4] = 0.0;
  output->J[0][5] = 1.0;   // body2 (rotating mass) angular velocity
  ```

- **Constraint Equation**: J * v + c = 0
  With c = v_bias = 0 (line 43), this becomes:
  ```
  -1 * v_theta_crankshaft + 1 * v_theta_rotatingMass = 0
  Therefore: v_theta_rotatingMass = v_theta_crankshaft
  ```

- **Torque Limits (lines 45-46)**:
  ```
  output->limits[0][0] = m_minTorque;
  output->limits[0][1] = m_maxTorque;
  ```

### Clutch Pressure Effect
From `/Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/src/transmission.cpp` (lines 33-41):

```cpp
void Transmission::update(double dt) {
    if (m_gear == -1) {
        m_clutchConstraint.m_minTorque = 0;
        m_clutchConstraint.m_maxTorque = 0;
    }
    else {
        m_clutchConstraint.m_minTorque = -m_maxClutchTorque * m_clutchPressure;
        m_clutchConstraint.m_maxTorque = m_maxClutchTorque * m_clutchPressure;
    }
}
```

At m_clutchPressure = 1.0:
- m_minTorque = -m_maxClutchTorque
- m_maxTorque = m_maxClutchTorque

### Behavior Analysis
The constraint solver attempts to enforce v_theta_rotatingMass = v_theta_crankshaft. The required constraint force (torque) is computed to satisfy this equation. If the required torque falls within [-m_maxClutchTorque, m_maxClutchTorque], the constraint maintains lock (rigid kinematic behavior). If the required torque exceeds these limits, the constraint is violated and slip occurs.

Therefore, even at max clutch pressure, the constraint **allows slip** when the torque required to maintain synchronization exceeds m_maxClutchTorque.

### Dynamometer Capability
From `/Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/src/dynamometer.cpp`:

- **Constraint Jacobian (lines 26-28)**:
  ```
  output->J[0][0] = 0;
  output->J[0][1] = 0;
  output->J[0][2] = 1;  // Connects to one body's angular velocity
  ```

- **Constraint Equation**: J * v + v_bias = 0
  ```
  1 * v_theta + v_bias = 0
  Therefore: v_theta = -v_bias
  ```

- **Velocity Bias (lines 39-47)**:
  ```cpp
  if (m_bodies[0]->v_theta < 0) {
      output->v_bias[0] = m_rotationSpeed;
      // ...
  } else {
      output->v_bias[0] = -m_rotationSpeed;
      // ...
  }
  ```

This shows the dynamometer constrains angular velocity to a target value (±m_rotationSpeed), **not** applying a specific torque. The torque limits determine how much torque can be applied to achieve that velocity.

When m_hold = true, it becomes a unidirectional torque limit:
- For v_theta < 0: output->limits[0][0] = -m_maxTorque (min torque), output->limits[0][1] = 0 (max torque)
- For v_theta >= 0: output->limits[0][0] = 0 (min torque), output->limits[0][1] = m_maxTorque (max torque)

**Conclusion on dynamometer**: The dynamometer primarily acts as an RPM holder, not a pure torque brake. It can function as a torque-limited device in one direction when m_hold=true, but cannot independently set both min and max torque limits simultaneously for bidirectional torque control.

### Drivetrain Load Computation
From `/Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/src/vehicle_drag_constraint.cpp` (lines 56-58):
```
output->limits[0][0] = -m_vehicle->linearForceToVirtualTorque(
    rollingResistance + 0.5 * airDensity * v_squared * c_d * A);
output->limits[0][1] = 0;
```
This shows drivetrain load is computed as a resistance torque (negative limit only) based on vehicle speed, drag coefficient, cross-sectional area, and rolling resistance.

### Transmission Gear Change
From `/Users/danielsinclair/vscode/escli.refac7/engine-sim-bridge/engine-sim/src/transmission.cpp` (lines 59-82):
- changeGear() calculates new moment of inertia (I) based on vehicle mass, gear ratio, diff ratio, and tire radius
- It preserves kinetic energy (E_r) when changing gears
- Angular velocity (v_theta) is adjusted to maintain energy: new_v_theta = ±√(E_r * 2 / new_I)
- **Does maintain angular velocity continuity** through energy conservation, not direct velocity preservation

## Final Recommendations

1. **Physics-driven RPM tracking viability**: **Not viable as primary mechanism**
   - The clutch constraint is slip-allowing even at max pressure
   - RPM tracking would require infinite m_maxClutchTorque to prevent slip under all loads
   - Real-world clutches have finite torque capacity

2. **Dyno-assist requirement**: **Required as primary mechanism**
   - Dyno can precisely control RPM (when m_hold=false)
   - Can provide bidirectional torque limiting when needed
   - Better suited for precise RPM control in simulation

3. **Architecture implication**: 
   - Use physics-based clutch for realistic slip behavior and torque transfer
   - Use dyno as primary RPM controller for test-cell simulation
   - Twin architecture should rely on dyno for RPM tracking, not clutch constraint