// ILogging.cpp - Default stderr logging implementation

#include "ILogging.h"
#include <cstdarg>
#include <cstdio>

const char* StdErrLogging::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

bool StdErrLogging::shouldLog(LogLevel level) {
    return level >= minLevel_;
}

void StdErrLogging::log(LogLevel level, const char* format, ...) {
    if (!shouldLog(level)) return;

    va_list args;
    va_start(args, format);

    const char* levelStr = levelToString(level);
    fprintf(stderr, "[%s] ", levelStr);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);

    va_end(args);
}

void StdErrLogging::debug(const char* format, ...) {
    if (!shouldLog(LogLevel::Debug)) return;

    va_list args;
    va_start(args, format);
    const char* levelStr = levelToString(LogLevel::Debug);
    fprintf(stderr, "[%s] ", levelStr);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
}

void StdErrLogging::info(const char* format, ...) {
    if (!shouldLog(LogLevel::Info)) return;

    va_list args;
    va_start(args, format);
    const char* levelStr = levelToString(LogLevel::Info);
    fprintf(stderr, "[%s] ", levelStr);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
}

void StdErrLogging::warning(const char* format, ...) {
    if (!shouldLog(LogLevel::Warning)) return;

    va_list args;
    va_start(args, format);
    const char* levelStr = levelToString(LogLevel::Warning);
    fprintf(stderr, "[%s] ", levelStr);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
}

void StdErrLogging::error(const char* format, ...) {
    if (!shouldLog(LogLevel::Error)) return;

    va_list args;
    va_start(args, format);
    const char* levelStr = levelToString(LogLevel::Error);
    fprintf(stderr, "[%s] ", levelStr);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(args);
}
