#!/usr/bin/env bash
set -euo pipefail

MODE=${1:-all}

if [ "${MODE}" = "staged" ]; then
  FILES=$(git diff --cached --name-only --diff-filter=ACM || true)
else
  FILES=$(git ls-files || true)
fi

if [ -z "${FILES}" ]; then
  exit 0
fi

# Exclude this checker script from the candidate file list to avoid self-matches
FILES=$(echo "${FILES}" | grep -v '^scripts/check-generated-files\.sh$' || true)

# 1) Reject files containing explicit CMake-generated header
if echo "${FILES}" | xargs grep -I -H "CMAKE generated file: DO NOT EDIT!" >/dev/null 2>&1; then
  echo "Error: Committed/generated CMake files detected:" >&2
  echo "" >&2
  echo "Files containing the marker:" >&2
  echo "${FILES}" | xargs grep -n "CMAKE generated file: DO NOT EDIT!" || true
  exit 1
fi

# 2) Reject common binary/build artefacts
BAD_BIN=$(echo "${FILES}" | grep -E '\\.o$|\\.a$|\\.so$|\\.dylib$' || true)
if [ -n "${BAD_BIN}" ]; then
  echo "Error: build artefact files staged/committed:" >&2
  echo "${BAD_BIN}" >&2
  exit 1
fi

# 3) Reject tracked build directories
BAD_DIR=$(echo "${FILES}" | grep -E '(^|/)(build|bin|build-tui)(/|$)' || true)
if [ -n "${BAD_DIR}" ]; then
  echo "Error: build directories should not be tracked:" >&2
  echo "${BAD_DIR}" >&2
  exit 1
fi

exit 0
