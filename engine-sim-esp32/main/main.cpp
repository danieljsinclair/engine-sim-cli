// main.cpp — ESP32-S3 entry point for engine-sim-cli
// Audio output via I2S to MAX98357A (GPIO 4=BCLK, 5=LRCLK, 6=DIN)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "strategy/IAudioBuffer.h"
#include "simulation/SimulationLoop.h"
#include "common/ILogging.h"
#include "telemetry/ITelemetryProvider.h"

static const char* TAG = "engine-sim-esp32";

// Default mode: sine wave. Override with menuconfig or Kconfig for other modes.
#ifndef ESP_SIM_MODE
#define ESP_SIM_MODE SINE
#endif

// Default audio strategy: SyncPull. Set to 1 for Threaded if needed.
#ifndef ESP_USE_THREADED
#define ESP_USE_THREADED 0
#endif

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "engine-sim-cli ESP32 starting...");

    auto logger = std::make_unique<ConsoleLogger>();
    auto telemetry = std::make_unique<telemetry::InMemoryTelemetry>();

    SimulationConfig config;
    config.engineConfig.sampleRate = EngineSimDefaults::SAMPLE_RATE;
    config.engineConfig.simulationFrequency = EngineSimDefaults::SIMULATION_FREQUENCY;
    config.engineConfig.fluidSimulationSteps = EngineSimDefaults::FLUID_SIMULATION_STEPS;
    config.engineConfig.targetSynthesizerLatency = EngineSimDefaults::TARGET_SYNTH_LATENCY;
    config.duration = 0;          // 0 = run indefinitely
    config.playAudio = true;
    config.syncPull = !ESP_USE_THREADED;
    config.volume = 0.8f;

    SimulatorType simType = SimulatorType::SineWave;
    const char* modeLabel = "sine";

    auto simulator = SimulatorFactory::create(
        simType,
        "", "",
        config.engineConfig,
        logger.get(),
        telemetry.get()
    );

    if (!simulator) {
        ESP_LOGE(TAG, "Failed to create %s simulator", modeLabel);
        return;
    }

    AudioMode audioMode = ESP_USE_THREADED ? AudioMode::Threaded : AudioMode::SyncPull;
    auto audioBuffer = IAudioBufferFactory::createBuffer(
        audioMode,
        logger.get(),
        telemetry.get()
    );

    ESP_LOGI(TAG, "Starting %s simulation (%s strategy)...",
             modeLabel, ESP_USE_THREADED ? "threaded" : "sync-pull");

    int result = runSimulation(
        config,
        *simulator,
        audioBuffer.get(),
        nullptr,               // no input provider (timed auto-throttle)
        nullptr,               // no presentation (ESP_LOG only)
        telemetry.get(),
        telemetry.get(),
        logger.get()
    );

    ESP_LOGI(TAG, "Simulation finished with result %d", result);
}
