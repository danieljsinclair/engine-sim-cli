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

    /// List of available engine preset names for the picker UI
    @Published var presetNames: [String] = []
    /// List of available engine preset IDs (parallel to presetNames)
    @Published var presetIds: [String] = []

    // MARK: - Private

    private var wrapper: EngineSimWrapper?
    private var updateTimer: Timer?
    private let updateInterval: TimeInterval = 0.1 // 10 Hz

    // MARK: - Lifecycle

    init() {
        wrapper = EngineSimWrapper()
        loadPresetList()
    }

    deinit {
        stop()
    }

    // MARK: - Preset List

    private func loadPresetList() {
        let count = EngineSimWrapper.presetCount()
        var names: [String] = []
        var ids: [String] = []
        for i in 0..<count {
            names.append(EngineSimWrapper.presetName(at: i))
            ids.append(EngineSimWrapper.presetId(at: i))
        }
        presetNames = names
        presetIds = ids
    }

    // MARK: - Public API

    /// Load a hardcoded engine preset by ID
    func loadPreset(_ presetId: String) {
        guard let wrapper = wrapper else {
            connectionStatus = "Wrapper not initialized"
            return
        }

        let success = wrapper.loadPreset(presetId)
        if success {
            connectionStatus = "Preset loaded: \(presetId)"
        } else {
            connectionStatus = "Failed to load preset: \(presetId)"
        }
    }

    func start() {
        guard let wrapper = wrapper else {
            connectionStatus = "Wrapper not initialized"
            return
        }

        _ = wrapper.startAudioThread()
        isConnected = true
        connectionStatus = "Running"

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
