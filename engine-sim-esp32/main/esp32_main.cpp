// esp32_main.cpp — ESP32-S3 entry point for engine-sim-cli
// Audio output via I2S to MAX98357A (GPIO 4=BCLK, 5=LRCLK, 6=DIN)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "simulator/SimulatorFactory.h"
#include "simulator/EngineSimTypes.h"
#include "strategy/IAudioBuffer.h"
#include "simulation/SimulationLoop.h"
#include "session/ISimulatorSession.h"
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

    config.simulatorType = SimulatorType::SineWave;
    AudioMode audioMode = ESP_USE_THREADED ? AudioMode::Threaded : AudioMode::SyncPull;

    ESP_LOGI(TAG, "Starting sine simulation (%s strategy)...",
             ESP_USE_THREADED ? "threaded" : "sync-pull");

    // Client creates simulator (DI — always explicit)
    auto simulator = SimulatorFactory::createAndConfigure(config, "", "", logger.get(), telemetry.get());

    // Client creates audio buffer (DI — always explicit)
    auto audioBuffer = IAudioBufferFactory::createBuffer(audioMode, logger.get(), telemetry.get());

    auto session = createSession(
        config,
        "",                  // no script for sine mode
        std::move(simulator),
        audioBuffer.get(),
        nullptr,             // no existing session (fresh start)
        nullptr,             // no input provider (timed auto-throttle)
        nullptr,             // no presentation (ESP_LOG only)
        telemetry.get(),     // telemetry writer
        telemetry.get(),     // telemetry reader
        logger.get()
    );

    int result = session->run();
    session->close();

    ESP_LOGI(TAG, "Simulation finished with result %d", result);
}
