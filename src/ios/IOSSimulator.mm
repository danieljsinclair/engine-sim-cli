// IOSSimulator.mm - iOS-specific simulator implementation
// Uses shared SimulationRunner from bridge for audio pipeline

#include "IOSSimulator.h"
#include "simulation/SimulationRunner.h"
#include "simulator/SimulatorFactory.h"

IOSSimulator::IOSSimulator()
    : running_(false)
{
    telemetry_ = std::make_unique<telemetry::InMemoryTelemetry>();
    runner_ = std::make_unique<SimulationRunner>();
}

IOSSimulator::~IOSSimulator() {
    stop();
}

bool IOSSimulator::loadScript(const std::string& scriptPath, const std::string& assetBasePath) {
    ISimulatorConfig config;
    simulator_ = SimulatorFactory::create(
        SimulatorType::PistonEngine,
        scriptPath,
        assetBasePath,
        config,
        nullptr,
        telemetry_.get()
    );

    if (!simulator_) return false;

    // Initialize audio pipeline via shared runner
    return runner_->initialize(*simulator_, nullptr, telemetry_.get());
}

bool IOSSimulator::start() {
    if (!simulator_ || !runner_) return false;

    // Start simulator
    if (!simulator_->start()) {
        return false;
    }

    // Start audio pipeline via shared runner
    running_ = runner_->start();
    return running_;
}

void IOSSimulator::stop() {
    if (simulator_) {
        if (running_) {
            runner_->stop();
            simulator_->stop();
        }
        simulator_->destroy();
        simulator_.reset();
    }
    running_ = false;
}

void IOSSimulator::update(double deltaTime) {
    runner_->update(deltaTime);

    // Write vehicle inputs to telemetry
    telemetry::VehicleInputsTelemetry inputs;
    inputs.throttlePosition = currentThrottle_;
    inputs.ignitionOn = currentIgnition_;
    inputs.starterMotorEngaged = currentStarter_;
    telemetry_->writeVehicleInputs(inputs);
}

void IOSSimulator::setThrottle(double position) {
    currentThrottle_ = position;
    if (simulator_) simulator_->setThrottle(position);
}

void IOSSimulator::setIgnition(bool on) {
    currentIgnition_ = on;
    if (simulator_) simulator_->setIgnition(on);
}

void IOSSimulator::setStarterMotor(bool on) {
    currentStarter_ = on;
    if (simulator_) simulator_->setStarterMotor(on);
}

double IOSSimulator::getCurrentRPM() const {
    if (simulator_) {
        auto stats = simulator_->getStats();
        return stats.currentRPM;
    }
    return 0.0;
}

double IOSSimulator::getCurrentLoad() const {
    if (simulator_) {
        auto stats = simulator_->getStats();
        return stats.currentLoad;
    }
    return 0.0;
}

double IOSSimulator::getExhaustFlow() const {
    if (simulator_) {
        auto stats = simulator_->getStats();
        return stats.exhaustFlow;
    }
    return 0.0;
}

double IOSSimulator::getManifoldPressure() const {
    if (simulator_) {
        auto stats = simulator_->getStats();
        return stats.manifoldPressure;
    }
    return 0.0;
}

double IOSSimulator::getThrottlePosition() const {
    if (telemetry_) {
        auto inputs = telemetry_->getVehicleInputs();
        return inputs.throttlePosition;
    }
    return 0.0;
}

bool IOSSimulator::getIgnition() const {
    if (telemetry_) {
        auto inputs = telemetry_->getVehicleInputs();
        return inputs.ignitionOn;
    }
    return false;
}

bool IOSSimulator::getStarterMotor() const {
    if (telemetry_) {
        auto inputs = telemetry_->getVehicleInputs();
        return inputs.starterMotorEngaged;
    }
    return false;
}

bool IOSSimulator::isRunning() const {
    return running_ && simulator_ != nullptr;
}
