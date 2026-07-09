// CliException.h - Dedicated exception type for CLI runtime failures
// Replaces generic std::runtime_error throws so callers can distinguish CLI
// failures by type. Derives from std::runtime_error (and therefore std::exception)
// so existing `catch (const std::exception&)` handlers still catch it and
// what() returns the same descriptive message.

#ifndef CLI_EXCEPTION_H
#define CLI_EXCEPTION_H

#include <stdexcept>

// Dedicated exception for CLI configuration / initialization / validation
// failures. Thrown from the CLI layer instead of the generic
// std::runtime_error (cpp:S112). Inherits std::runtime_error semantics so the
// what() message and catch behaviour are unchanged.
class CliException : public std::runtime_error {
public:
    // Inherit std::runtime_error's constructors so CliException can be thrown
    // with a string literal or a std::string, matching how it is used at the
    // CLI call sites (cpp:S5952).
    using std::runtime_error::runtime_error;
};

#endif // CLI_EXCEPTION_H
