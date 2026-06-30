// CliException.h - Dedicated exception type for CLI runtime failures
// Replaces generic std::runtime_error throws so callers can distinguish CLI
// failures by type. Derives from std::runtime_error (and therefore std::exception)
// so existing `catch (const std::exception&)` handlers still catch it and
// what() returns the same descriptive message.

#ifndef CLI_EXCEPTION_H
#define CLI_EXCEPTION_H

#include <stdexcept>
#include <string>

// Dedicated exception for CLI configuration / initialization / validation
// failures. Thrown from the CLI layer instead of the generic
// std::runtime_error (cpp:S112). Inherits std::runtime_error semantics so the
// what() message and catch behaviour are unchanged.
class CliException : public std::runtime_error {
public:
    explicit CliException(const std::string& message)
        : std::runtime_error(message) {
    }
};

#endif // CLI_EXCEPTION_H
