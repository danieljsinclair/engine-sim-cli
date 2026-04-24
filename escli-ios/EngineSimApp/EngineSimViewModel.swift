import Foundation
import Combine

/// Observable view model for SwiftUI engine telemetry display.
/// Reads state from C++ via EngineSimWrapper (thin bridge to iOSPresentation/ITelemetryReader).
/// Sends controls to C++ via EngineSimWrapper (thin bridge to iOSInputProvider).
///
/// Architecture:
/// - Swift UI -> EngineSimViewModel -> EngineSimWrapper -> C++ (iOSInputProvider/iOSPresentation)
/// - All simulation logic is in C++ (ios_main entry point follows CLIMain.cpp pattern)
class EngineSimViewModel: ObservableObject {
    // MARK: - Published Properties (telemetry from C++)

    @Published var rpm: Double = 0.0
    @Published var load: Double = 0.0
    @Published var exhaustFlow: Double = 0.0
    @Published var manifoldPressure: Double = 0.0

    // MARK: - Published Properties (control state from C++ telemetry)

    @Published var throttlePosition: Double = 0.0 {
        didSet {
            // Forward UI changes immediately to C++ (avoid telemetry round-trip lag)
            wrapper?.setThrottle(throttlePosition)
        }
    }
    @Published var ignitionEnabled: Bool = false {
        didSet { wrapper?.setIgnition(ignitionEnabled) }
    }
    @Published var starterEnabled: Bool = false {
        didSet { wrapper?.setStarter(starterEnabled) }
    }

    @Published var isConnected: Bool = false
    @Published var connectionStatus: String = "Disconnected"

    // Audio diagnostics
    @Published var underrunCount: Int = 0

    // MARK: - Private

    private var wrapper: EngineSimWrapper?
    private var updateTimer: Timer?
    private let updateInterval: TimeInterval = 0.05 // 20 Hz (faster than before)

    // MARK: - Lifecycle

    init() {
        wrapper = EngineSimWrapper()
    }

    deinit {
        stop()
    }

    // MARK: - Public API

    func start() {
        guard let wrapper = wrapper else {
            connectionStatus = "Wrapper not initialized"
            return
        }

        // Start C++ simulation (uses sine mode by default)
        // C++ handles all setup: SimulatorFactory -> IAudioBufferFactory -> runSimulation()
        if wrapper.start() {
            isConnected = true
            connectionStatus = "Running (Sine Mode)"

            // Sync initial state from C++
            syncState()

            // Start telemetry polling timer
            updateTimer = Timer.scheduledTimer(
                withTimeInterval: updateInterval,
                repeats: true
            ) { [weak self] _ in
                self?.updateTelemetry()
            }
        } else {
            connectionStatus = "Failed to start"
        }
    }

    func stop() {
        updateTimer?.invalidate()
        updateTimer = nil
        wrapper?.stop()
        isConnected = false
        connectionStatus = "Disconnected"

        // Sync final state from C++
        syncState()
    }

    // MARK: - Control Methods (forward to C++)

    func setThrottle(_ position: Double) {
        wrapper?.setThrottle(position)
        // Don't update local state here - C++ owns the state
        // syncState() will read it back on next timer tick
    }

    func setIgnition(_ enabled: Bool) {
        wrapper?.setIgnition(enabled)
    }

    func setStarter(_ enabled: Bool) {
        wrapper?.setStarter(enabled)
    }

    // MARK: - Private

    private func updateTelemetry() {
        guard let wrapper = wrapper, wrapper.isRunning else {
            // Simulation stopped
            isConnected = false
            connectionStatus = "Stopped"
            return
        }

        // Read all state from C++ (single atomic read per value)
        self.rpm = wrapper.currentRPM
        self.load = wrapper.currentLoad
        self.exhaustFlow = wrapper.exhaustFlow
        self.manifoldPressure = wrapper.manifoldPressure
        self.underrunCount = Int(wrapper.underrunCount)

        // Controls are write-only from UI → C++; do NOT sync back from telemetry
        // (avoids feedback loops and unnecessary property writes)
    }

    private func syncState() {
        guard let wrapper = wrapper else { return }

        // Only sync initial state from C++ on start/stop — controls are write-only during runtime
        // This is safe: wrapper state represents the last user input that was stored
        self.throttlePosition = wrapper.throttlePosition
        self.ignitionEnabled = wrapper.ignitionEnabled
        self.starterEnabled = wrapper.starterMotorEnabled
    }
}
