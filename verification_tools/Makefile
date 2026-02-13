# Makefile for Engine-Sim-CLI Verification Tools

CC = gcc
CFLAGS = -Wall -Wextra -O2 -lm
TARGETS = verify_audio verify_input verify_engine
SCRIPTS = ../verification_system.sh ../ci_verification.sh

# Default target
.PHONY: all
all: $(TARGETS)

# Build verification tools
verify_audio: verify_audio.c
	$(CC) $(CFLAGS) -o $@ $<

verify_input: verify_input.c
	$(CC) $(CFLAGS) -o $@ $<

verify_engine: verify_engine.c
	$(CC) $(CFLAGS) -o $@ $<

# Make scripts executable
.PHONY: scripts
scripts:
	@echo "Making scripts executable..."
	@chmod +x $(SCRIPTS)

# Run all verification tests
.PHONY: test
test: $(TARGETS) scripts
	@echo "========================================"
	@echo "Running full verification test suite"
	@echo "========================================"
	../$(SCRIPTS) --engine-sim-cli ../engine-sim-cli

# Run quick verification (critical tests only)
.PHONY: test-quick
test-quick: scripts
	@echo "======================================"
	@echo "Running quick verification (critical)"
	@echo "======================================"
	../$(SCRIPTS) --mode binary --engine-sim-cli ../engine-sim-cli
	../$(SCRIPTS) --mode config --engine-sim-cli ../engine-sim-cli
	../$(SCRIPTS) --mode engine --engine-sim-cli ../engine-sim-cli --duration 5

# Run CI verification
.PHONY: ci
ci: $(TARGETS) scripts
	@echo "=================================="
	@echo "Running CI verification"
	@echo "=================================="
	../$(CI_SCRIPT) --quick

# Run CI verification with parallel tests
.PHONY: ci-parallel
ci-parallel: $(TARGETS) scripts
	@echo "========================================="
	@echo "Running CI verification with parallel tests"
	@echo "========================================="
	../$(CI_SCRIPT) --parallel --engine-sim-cli ../engine-sim-cli

# Test specific components
.PHONY: test-audio
test-audio: verify_audio
	@echo "======================"
	@echo "Testing audio output"
	@echo "======================"
	../$(SCRIPTS) --mode audio --engine-sim-cli ../engine-sim-cli

.PHONY: test-input
test-input: verify_input
	@echo "======================"
	@echo "Testing input response"
	@echo "======================"
	../$(SCRIPTS) --mode input --engine-sim-cli ../engine-sim-cli

.PHONY: test-engine
test-engine: verify_engine
	@echo "======================="
	@echo "Testing engine startup"
	@echo "======================="
	../$(SCRIPTS) --mode engine --engine-sim-cli ../engine-sim-cli

# Check if engine-sim-cli exists
.PHONY: check-prerequisites
check-prerequisites:
	@echo "Checking prerequisites..."
	@if [ ! -f ../engine-sim-cli ]; then \
		echo "ERROR: engine-sim-cli not found in parent directory"; \
		exit 1; \
	fi
	@echo "Prerequisites check passed"

# Run verification with full output
.PHONY: verify
verify: $(TARGETS) scripts
	../$(SCRIPTS) --engine-sim-cli ../engine-sim-cli --verbose --output-dir ../verification_results

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(TARGETS)

# Clean everything including reports
.PHONY: clean-all
clean-all: clean
	rm -rf ../verification_output/
	rm -rf ../ci_reports/
	rm -f ../*.wav
	rm -f ../verification_results/* -rf 2>/dev/null || true

# Show help
.PHONY: help
help:
	@echo "Engine-Sim-CLI Verification System"
	@echo "==================================="
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build all verification tools"
	@echo "  test        - Run all verification tests"
	@echo "  test-quick  - Run critical tests only (fast)"
	@echo "  ci          - Run CI verification (quick mode)"
	@echo "  ci-parallel - Run CI verification with parallel tests"
	@echo "  test-audio  - Test audio output only"
	@echo "  test-input  - Test input response only"
	@echo "  test-engine - Test engine startup only"
	@echo "  clean       - Clean build artifacts"
	@echo "  clean-all   - Clean all generated files"
	@echo "  check-prerequisites - Verify prerequisites exist"
	@echo "  verify      - Run verification with verbose output"
	@echo ""
	@echo "Manual commands:"
	@echo "  ../verification_system.sh --help    # Show verification script help"
	@echo "  ../ci_verification.sh --help       # Show CI script help"