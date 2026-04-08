// IAudioStrategyFactory.cpp - Factory for creating audio strategies
// Implements the factory pattern to create appropriate IAudioStrategy implementations

#include "audio/strategies/IAudioStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"

#include <memory>

// Forward declarations for strategy implementations
// Will be implemented when we create the strategy classes

// Implementation of createStrategy method
std::unique_ptr<IAudioStrategy> IAudioStrategyFactory::createStrategy(
    AudioMode mode,
    ILogging* logger
) {
    switch (mode) {
        case AudioMode::Threaded: return std::make_unique<ThreadedStrategy>(logger);
        case AudioMode::SyncPull: return std::make_unique<SyncPullStrategy>(logger);
    }

    return nullptr;
}