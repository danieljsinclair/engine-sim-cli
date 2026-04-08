// StrategyAdapterFactory.h - Factory for creating StrategyAdapter instances
// Bridges new IAudioStrategy architecture to old IAudioRenderer interface
// SOLID: Factory pattern for creating adapted strategies

#ifndef STRATEGY_ADAPTER_FACTORY_H
#define STRATEGY_ADAPTER_FACTORY_H

#include <memory>
#include "audio/strategies/IAudioStrategy.h"  // Factory is defined here
#include "audio/adapters/StrategyAdapter.h"
#include "audio/renderers/IAudioRenderer.h"
#include "ILogging.h"

/**
 * Create a StrategyAdapter that bridges IAudioStrategy to IAudioRenderer
 *
 * This factory function allows CLIMain and other code to use the new
 * IAudioStrategy architecture while maintaining the old IAudioRenderer interface.
 *
 * @param syncPullMode If true, creates SyncPullStrategy, otherwise ThreadedStrategy
 * @param logger Optional logger for diagnostics
 * @return Unique pointer to IAudioRenderer (actually a StrategyAdapter)
 *
 * SRP: Only creates adapters, no business logic
 * OCP: Can add new strategies without modifying this function
 * DIP: Depends on abstractions, not concrete implementations
 */
inline std::unique_ptr<IAudioRenderer> createStrategyAdapter(
    bool syncPullMode,
    ILogging* logger = nullptr
) {
    // Determine AudioMode from syncPull flag
    AudioMode mode = syncPullMode ? AudioMode::SyncPull : AudioMode::Threaded;

    // Create the IAudioStrategy using the factory
    std::unique_ptr<IAudioStrategy> strategy = IAudioStrategyFactory::createStrategy(mode, logger);

    if (!strategy) {
        return nullptr;
    }

    // Create StrategyContext
    auto context = std::make_unique<StrategyContext>();

    // Create and return the adapter (pass logger for diagnostics)
    return std::make_unique<StrategyAdapter>(
        std::move(strategy),
        std::move(context),
        logger
    );
}

#endif // STRATEGY_ADAPTER_FACTORY_H
