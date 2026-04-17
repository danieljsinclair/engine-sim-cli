import Foundation
import Combine

/// Observable view model for SwiftUI engine telemetry display.
/// Maps directly from C++ EngineSimStats via EngineSimWrapper.
class EngineSimViewModel: ObservableObject {
    // MARK: - Published Properties (match EngineSimStats exactly)

    @Published var rpm: Double = 0.0
    @Published var load: Double = 0.0
    @Published var exhaustFlow: Double = 0.0
    @Published var manifoldPressure: Double = 0.0
    @Published var throttlePosition: Double = 0.0
    @Published var ignitionEnabled: Bool = false
    @Published var starterEnabled: Bool = false

    @Published var isConnected: Bool = false
    @Published var connectionStatus: String = "Disconnected"

    // MARK: - Private

    private var wrapper: EngineSimWrapper?
    private var updateTimer: Timer?
    private let updateInterval: TimeInterval = 0.1 // 10 Hz

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

        // Use sine mode for demo (no script file needed)
        // In production, loadScript would load a .mr file from the app bundle
        _ = wrapper.startAudioThread()
        isConnected = true
        connectionStatus = "Running (Demo)"

        updateTimer = Timer.scheduledTimer(
            withTimeInterval: updateInterval,
            repeats: true
        ) { [weak self] _ in
            self?.updateTelemetry()
        }
    }

    func stop() {
        updateTimer?.invalidate()
        updateTimer = nil
        wrapper?.stop()
        isConnected = false
        connectionStatus = "Disconnected"
    }

    func setThrottle(_ position: Double) {
        throttlePosition = position
        wrapper?.setThrottle(position)
    }

    func setIgnition(_ enabled: Bool) {
        ignitionEnabled = enabled
        wrapper?.setIgnition(enabled)
    }

    func setStarter(_ enabled: Bool) {
        starterEnabled = enabled
        wrapper?.setStarter(enabled)
    }

    // MARK: - Private

    private func updateTelemetry() {
        guard let wrapper = wrapper else { return }

        wrapper.update(1.0 / 60.0)
        self.rpm = wrapper.currentRPM
        self.load = wrapper.currentLoad
        self.exhaustFlow = wrapper.exhaustFlow
        self.manifoldPressure = wrapper.manifoldPressure
    }
}
