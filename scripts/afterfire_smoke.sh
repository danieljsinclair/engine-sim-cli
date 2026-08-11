#!/bin/bash
#
# afterfire_smoke.sh - Deterministic, non-interactive proof that afterfire pops fire.
#
# Drives the C63 M156 V3 through a rev-then-cut cycle using a generated
# telemetry CSV (the only input expression that can encode "full throttle, THEN
# throttle 0" over time -- --throttle latches ONE constant value for the whole
# run, so it can never produce the overrun transition a pop requires), then
# parses --afterfire-diagnostics and asserts events > 0.
#
# No keyboard, no human, no audio device interaction (--silent runs the full
# audio pipeline at zero volume).
#
# Usage: from the engine-sim-cli repo root:
#   ./scripts/afterfire_smoke.sh
#   ./scripts/afterfire_smoke.sh --binary build/engine-sim-cli   # non-default build dir
#   ./scripts/afterfire_smoke.sh --keep-log                      # retain run output
#
# Exit code: 0 = pops fired (events > 0); non-zero = no pops, or a setup failure.
#

set -uo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Default to the Makefile's BUILD_DIR (build/), NOT build-cli/. They can both
# exist, and a stale build-cli/ binary silently passes this check against
# old code — which is exactly what happened once and cost real debugging time.
BINARY="build/engine-sim-cli"
SCRIPT_MR="es/C63_M156_V3.mr"
CSV="scripts/fixtures/afterfire_rev_cut.csv"
KEEP_LOG=0

# Length of the generated trace, and the run's hard stop.
#
# --duration is NOT optional here. Without it, CLIconfig's processArgs() forces
# interactive=true whenever --duration is absent; CLIMain's "default the duration
# to the trace length" branch is guarded on !interactive, so it never fires and
# the replay clamps to its last CSV sample and re-runs that frame forever. An
# explicit duration keeps the run non-interactive AND bounded — which is the
# whole point of an unattended smoke check.
TRACE_SECONDS=13

# Afterfire settings for the smoke run: the SHIPPED DEFAULTS, unmodified.
#
# Nothing needs relaxing any more. The model is physical -- a pop happens when a
# misfiring (deep-vacuum) cycle pumps raw fuel into a runner that is hot enough
# to auto-ignite it before the pipe scavenges it away -- so there is no
# probability coin-flip to pin to 1.0 and no RPM band to widen. That makes this
# check strictly stronger than before: it proves the effect works AS SHIPPED
# rather than only under smoke-test-only tuning.
AF_ARGS=(
  --enable-afterfire
  --afterfire-diagnostics
)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)    BINARY="$2"; shift 2 ;;
    --script)    SCRIPT_MR="$2"; shift 2 ;;
    --keep-log)  KEEP_LOG=1; shift ;;
    -h|--help)   sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; exit 2 ;;
  esac
done

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; RESET=$'\033[0m'
pass() { echo "${GREEN}PASS${RESET}  $*"; }
fail() { echo "${RED}FAIL${RESET}  $*" >&2; }
info() { echo "${YELLOW}....${RESET}  $*"; }

# ---------------------------------------------------------------------------
# Preflight -- fail fast with a named path rather than a confusing later error
# ---------------------------------------------------------------------------
if [[ ! -x "$BINARY" ]]; then
  fail "CLI binary not found or not executable: $REPO_ROOT/$BINARY"
  echo "      Build it first (make build), or pass --binary <path>." >&2
  exit 2
fi

if [[ ! -f "$SCRIPT_MR" ]]; then
  fail "Engine script not found: $REPO_ROOT/$SCRIPT_MR"
  exit 2
fi

# The engine needs its impulse-response WAVs; es_new/ has the .mr files but an
# EMPTY sound-library/, which would produce a silent (and pop-less) run.
ASSET_DIR="$(dirname "$SCRIPT_MR")"
if [[ ! -d "$ASSET_DIR/sound-library/smooth" ]] \
   || [[ -z "$(ls -A "$ASSET_DIR/sound-library/smooth" 2>/dev/null)" ]]; then
  fail "Impulse-response WAVs missing or empty: $ASSET_DIR/sound-library/smooth"
  echo "      Run against a tree with the assets present (es/, not es_new/)." >&2
  exit 2
fi

# ---------------------------------------------------------------------------
# Generate the rev-then-cut telemetry CSV
#
# The pop gate needs a genuine overrun: throttle at/below the cutoff WHILE the
# engine is spinning fast and decaying. So the trace is three phases:
#   0-2s   throttle 0  -- crank and catch (--start is implicit for replay)
#   2-7s   throttle 100 -- rev to the top of the range
#   7-13s  throttle 0  -- the cut; RPM coasts down => pops fire here
# Neutral throughout (gear_selector N) so the engine free-revs and the coast is
# engine-braking only, which is the cleanest pop condition.
# ---------------------------------------------------------------------------
mkdir -p "$(dirname "$CSV")"
info "Generating rev-then-cut trace: $CSV"
# The trace end is passed in so the CSV and --duration cannot drift apart.
python3 - "$CSV" "$TRACE_SECONDS" <<'PY'
import sys

HZ = 50
end = float(sys.argv[2])
# (start, end, throttle_pct): crank/catch, rev to the top, then the cut.
PHASES = [(0.0, 2.0, 0.0), (2.0, 7.0, 100.0), (7.0, end, 0.0)]

with open(sys.argv[1], "w") as f:
    f.write("time_s,throttle_pct,road_speed_kmh,gear,gear_selector,clutch_pct\n")
    for t0, t1, throttle in PHASES:
        for i in range(int((t1 - t0) * HZ)):
            f.write(f"{t0 + i / HZ:.4f},{throttle:.1f},0,0,N,0\n")
PY

if [[ ! -s "$CSV" ]]; then
  fail "Failed to generate telemetry CSV: $CSV"
  exit 2
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
LOG="$(mktemp -t afterfire_smoke)"
cleanup() { [[ "$KEEP_LOG" -eq 1 ]] || rm -f "$LOG"; }
trap cleanup EXIT

info "Running $SCRIPT_MR through the rev-then-cut trace (this takes a few minutes)"
"$BINARY" \
  --script "$SCRIPT_MR" \
  --start \
  --replay-telemetry "$CSV" \
  --duration "$TRACE_SECONDS" \
  --silent \
  "${AF_ARGS[@]}" \
  > "$LOG" 2>&1
RUN_EXIT=$?

[[ "$KEEP_LOG" -eq 1 ]] && info "Run log retained at: $LOG"

if [[ $RUN_EXIT -ne 0 ]]; then
  fail "CLI exited $RUN_EXIT -- the run itself failed, so pops were never evaluated."
  grep -Ei "error|failed|terminating" "$LOG" | head -10 >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Assert: events > 0
#
# Distinguish three outcomes deliberately:
#   - no diagnostics block  => counters unobservable (spike not compiled in, or
#                              no engine loaded). NOT the same as "0 events".
#   - events == 0           => the gates ran and refused; print the skip tallies,
#                              which say WHICH gate rejected every candidate.
#   - events > 0            => pops fired.
# ---------------------------------------------------------------------------
if grep -q "Afterfire diagnostics: unavailable" "$LOG"; then
  fail "Afterfire counters unavailable -- engine-sim built without"
  echo "      ATG_ENGINE_SIM_AFTERFIRE_SPIKE, or no engine loaded." >&2
  exit 1
fi

EVENTS="$(grep -E "^[[:space:]]*events[[:space:]]*=" "$LOG" | head -1 | sed -E 's/.*=[[:space:]]*([0-9]+).*/\1/')"

if [[ -z "$EVENTS" ]]; then
  fail "Could not parse an afterfire event count from the run output."
  echo "      Expected an '--afterfire-diagnostics' block; got:" >&2
  tail -20 "$LOG" >&2
  exit 1
fi

echo
grep -A 3 "Afterfire diagnostics" "$LOG" || true
echo

if [[ "$EVENTS" -gt 0 ]]; then
  pass "Afterfire pops fired: events = $EVENTS (expected > 0)"
  exit 0
fi

fail "No afterfire pops fired: events = 0"
echo "      The skip counters above name the gate that rejected every candidate." >&2
exit 1
