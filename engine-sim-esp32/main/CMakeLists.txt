# engine-sim-esp32/main — ESP-IDF component registration
# Compiles engine-sim-bridge + engine-sim sources directly into the main component

set(BRIDGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../engine-sim-bridge")
set(ENGINE_SIM_ROOT "${BRIDGE_ROOT}/engine-sim")
set(SCS_ROOT "${ENGINE_SIM_ROOT}/dependencies/submodules/simple-2d-constraint-solver")

# Bridge sources (ESP32 subset — no piranha, no macOS/iOS audio providers)
set(BRIDGE_SOURCES
    ${BRIDGE_ROOT}/src/simulator/SineSimulator.cpp
    ${BRIDGE_ROOT}/src/simulator/SimulatorInitHelpers.cpp
    ${BRIDGE_ROOT}/src/simulator/SimulatorFactory.cpp
    ${BRIDGE_ROOT}/src/simulator/BridgeSimulator.cpp
    ${BRIDGE_ROOT}/src/simulator/ScriptLoadHelpers.cpp
    ${BRIDGE_ROOT}/src/simulator/ISimulator.cpp
    ${BRIDGE_ROOT}/src/strategy/ThreadedStrategy.cpp
    ${BRIDGE_ROOT}/src/strategy/SyncPullStrategy.cpp
    ${BRIDGE_ROOT}/src/strategy/IAudioBufferFactory.cpp
    ${BRIDGE_ROOT}/src/hardware/AudioHardwareProviderFactory.cpp
    ${BRIDGE_ROOT}/src/hardware/ESP32I2SHardwareProvider.cpp
    ${BRIDGE_ROOT}/src/telemetry/InMemoryTelemetry.cpp
    ${BRIDGE_ROOT}/src/simulation/SimulationLoop.cpp
    ${BRIDGE_ROOT}/src/common/ConsoleLogger.cpp
    ${BRIDGE_ROOT}/src/common/CircularBuffer.cpp
)

# Engine-sim core — explicit source list (file(GLOB) doesn't work in ESP-IDF's two-phase CMake)
# Excluded: GUI/display/gauge files (*_object.cpp depend on delta-studio rendering lib)
set(ENGINE_SIM_SOURCES
    ${ENGINE_SIM_ROOT}/src/audio_buffer.cpp
    ${ENGINE_SIM_ROOT}/src/camshaft.cpp
    ${ENGINE_SIM_ROOT}/src/combustion_chamber.cpp
    ${ENGINE_SIM_ROOT}/src/connecting_rod.cpp
    ${ENGINE_SIM_ROOT}/src/convolution_filter.cpp
    ${ENGINE_SIM_ROOT}/src/crankshaft.cpp
    ${ENGINE_SIM_ROOT}/src/cylinder_bank.cpp
    ${ENGINE_SIM_ROOT}/src/cylinder_head.cpp
    ${ENGINE_SIM_ROOT}/src/delay_filter.cpp
    ${ENGINE_SIM_ROOT}/src/derivative_filter.cpp
    ${ENGINE_SIM_ROOT}/src/direct_throttle_linkage.cpp
    ${ENGINE_SIM_ROOT}/src/dynamometer.cpp
    ${ENGINE_SIM_ROOT}/src/engine.cpp
    ${ENGINE_SIM_ROOT}/src/exhaust_system.cpp
    ${ENGINE_SIM_ROOT}/src/feedback_comb_filter.cpp
    ${ENGINE_SIM_ROOT}/src/filter.cpp
    ${ENGINE_SIM_ROOT}/src/fuel.cpp
    ${ENGINE_SIM_ROOT}/src/function.cpp
    ${ENGINE_SIM_ROOT}/src/gas_system.cpp
    ${ENGINE_SIM_ROOT}/src/gaussian_filter.cpp
    ${ENGINE_SIM_ROOT}/src/governor.cpp
    ${ENGINE_SIM_ROOT}/src/ignition_module.cpp
    ${ENGINE_SIM_ROOT}/src/impulse_response.cpp
    ${ENGINE_SIM_ROOT}/src/intake.cpp
    ${ENGINE_SIM_ROOT}/src/jitter_filter.cpp
    ${ENGINE_SIM_ROOT}/src/leveling_filter.cpp
    ${ENGINE_SIM_ROOT}/src/low_pass_filter.cpp
    ${ENGINE_SIM_ROOT}/src/part.cpp
    ${ENGINE_SIM_ROOT}/src/piston.cpp
    ${ENGINE_SIM_ROOT}/src/piston_engine_simulator.cpp
    ${ENGINE_SIM_ROOT}/src/simulator.cpp
    ${ENGINE_SIM_ROOT}/src/standard_valvetrain.cpp
    ${ENGINE_SIM_ROOT}/src/starter_motor.cpp
    ${ENGINE_SIM_ROOT}/src/synthesizer.cpp
    ${ENGINE_SIM_ROOT}/src/throttle.cpp
    ${ENGINE_SIM_ROOT}/src/transmission.cpp
    ${ENGINE_SIM_ROOT}/src/utilities.cpp
    ${ENGINE_SIM_ROOT}/src/valvetrain.cpp
    ${ENGINE_SIM_ROOT}/src/vehicle.cpp
    ${ENGINE_SIM_ROOT}/src/vehicle_drag_constraint.cpp
    ${ENGINE_SIM_ROOT}/src/vtec_valvetrain.cpp
)

# simple-2d-constraint-solver (physics dependency of engine-sim)
set(SCS_SOURCES
    ${SCS_ROOT}/src/clutch_constraint.cpp
    ${SCS_ROOT}/src/conjugate_gradient_sle_solver.cpp
    ${SCS_ROOT}/src/constant_rotation_constraint.cpp
    ${SCS_ROOT}/src/constant_speed_motor.cpp
    ${SCS_ROOT}/src/constraint.cpp
    ${SCS_ROOT}/src/euler_ode_solver.cpp
    ${SCS_ROOT}/src/fixed_position_constraint.cpp
    ${SCS_ROOT}/src/fixed_rotation_constraint.cpp
    ${SCS_ROOT}/src/force_generator.cpp
    ${SCS_ROOT}/src/gauss_seidel_sle_solver.cpp
    ${SCS_ROOT}/src/gaussian_elimination_sle_solver.cpp
    ${SCS_ROOT}/src/generic_rigid_body_system.cpp
    ${SCS_ROOT}/src/gravity_force_generator.cpp
    ${SCS_ROOT}/src/line_constraint.cpp
    ${SCS_ROOT}/src/link_constraint.cpp
    ${SCS_ROOT}/src/matrix.cpp
    ${SCS_ROOT}/src/nsv_ode_solver.cpp
    ${SCS_ROOT}/src/ode_solver.cpp
    ${SCS_ROOT}/src/optimized_nsv_rigid_body_system.cpp
    ${SCS_ROOT}/src/rigid_body.cpp
    ${SCS_ROOT}/src/rigid_body_system.cpp
    ${SCS_ROOT}/src/rk4_ode_solver.cpp
    ${SCS_ROOT}/src/rolling_constraint.cpp
    ${SCS_ROOT}/src/rotation_friction_constraint.cpp
    ${SCS_ROOT}/src/simple_gear_constraint.cpp
    ${SCS_ROOT}/src/sle_solver.cpp
    ${SCS_ROOT}/src/sparse_matrix.cpp
    ${SCS_ROOT}/src/spring.cpp
    ${SCS_ROOT}/src/static_force_generator.cpp
    ${SCS_ROOT}/src/system_state.cpp
    ${SCS_ROOT}/src/utilities.cpp
)

idf_component_register(
    SRCS
        esp32_main.cpp
        ${BRIDGE_SOURCES}
        ${ENGINE_SIM_SOURCES}
        ${SCS_SOURCES}
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/../stubs
        ${BRIDGE_ROOT}/include
        ${BRIDGE_ROOT}
        ${ENGINE_SIM_ROOT}/dependencies/dr_libs
        ${ENGINE_SIM_ROOT}/include
        ${ENGINE_SIM_ROOT}/scripting/include
        ${SCS_ROOT}/include
)

# ESP_PLATFORM selects I2S provider in factory
# __forceinline compat for simple-2d-constraint-solver (MSVC -> GCC)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ESP_PLATFORM
    __forceinline=inline
)

# Suppress GCC warnings that are fatal under ESP-IDF's -Werror
# but don't fire on Apple Clang:
# -Wchanges-meaning: engine-sim uses type names as member names (Valvetrain *Valvetrain)
# -Wnarrowing: int -> uint32_t in I2S config structs
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-changes-meaning
    -Wno-narrowing
)
