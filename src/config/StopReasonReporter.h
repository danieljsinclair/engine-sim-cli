// StopReasonReporter.h - Honest playback stop-reason reporting
//
// Extracted from CLIMain.cpp (ReplayTimeValidator precedent) so the decision
// is unit-testable: which console message applies when a playback run ends.
// The old inline version printed "<config.duration>s duration reached"
// whenever duration was set, regardless of what actually ended the run — so a
// --replay-telemetry --end-at 5 run over a 536s capture claimed "536.726s
// duration reached" (owner-reported 2026-08-30). The message now honours the
// actual cause: the --end-at bound (reported by the input provider), provider
// disconnect (live stream EOF), the duration timer, or the trace-end fallback
// for unbounded streaming runs.

#ifndef STOP_REASON_REPORTER_H
#define STOP_REASON_REPORTER_H

#include <string>

struct SimulationConfig;
namespace input { class IInputProvider; }

// Pure decision: the console message for how playback stopped. Parameters:
//   interactive    - interactive mode ended by the user (Q / Ctrl-C).
//   durationS      - config.duration (0 = no time limit; replay defaults to
//                    the full trace length at wiring time).
//   endAtReached   - the input provider hit its --end-at bound (it reported
//                    EOF because its elapsed clock crossed the requested
//                    timecode).
//   endAtS         - the requested --end-at bound (-1 = none requested).
//   inputExhausted - the provider is disconnected post-run (the live stream
//                    ended; data exhausted).
//   endAtClamped   - the requested --end-at/--duration window exceeded the
//                    trace length, so the bound was discarded and the run
//                    played to the trace end instead. Without this flag a
//                    clamped run would report the trace length as a "duration
//                    reached" stop (owner-reported 2026-09-08: --duration 200
//                    on a 158.982s capture claimed "158.982s duration reached").
std::string playbackStopMessage(bool interactive, double durationS,
                                bool endAtReached, double endAtS,
                                bool inputExhausted, bool endAtClamped);

// Print the stop reason for a finished run, reading the provider's post-run
// state (endAtReached / disconnected). provider may be null (interactive).
// endAtClamped: the requested --end-at/--duration window exceeded the trace
// length, so the run played to the trace end (see playbackStopMessage).
void reportStopReason(const SimulationConfig& config,
                      const input::IInputProvider* provider, double endAtS,
                      bool endAtClamped);

#endif // STOP_REASON_REPORTER_H
