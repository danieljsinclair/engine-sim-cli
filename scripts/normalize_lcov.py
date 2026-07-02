#!/usr/bin/env python3
"""Normalize lcov.info absolute paths to relative paths for SonarCloud.

lcov files from llvm-cov often contain absolute paths. SonarCloud's CFamily
sensor expects paths relative to the project root (where sonar-project.properties
lives). This script reads an lcov.info and rewrites SF: lines to be relative to
the given project root.
"""

import os
import sys
import argparse


def normalize_lcov(input_path: str, output_path: str, project_root: str) -> None:
    """Read lcov at input_path, normalize SF: paths, write to output_path."""
    with open(input_path, 'r') as f:
        lines = f.readlines()

    normalized = []
    for line in lines:
        if line.startswith('SF:'):
            abs_path = line[3:].rstrip('\n')
            try:
                # Make path relative to project root
                rel_path = os.path.relpath(abs_path, project_root)
                # If it goes outside project root (..), keep absolute (Sonar can handle it)
                if rel_path.startswith('..'):
                    normalized.append(line)
                else:
                    normalized.append(f'SF:{rel_path}\n')
            except ValueError:
                # Cross-device paths on Windows, keep absolute
                normalized.append(line)
        else:
            normalized.append(line)

    with open(output_path, 'w') as f:
        f.writelines(normalized)


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Normalize lcov absolute paths to relative paths for SonarCloud'
    )
    parser.add_argument('input', help='Input lcov.info file')
    parser.add_argument('output', help='Output lcov.info file')
    parser.add_argument('--project-root', required=True, help='Project root directory')
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f'Error: input file not found: {args.input}', file=sys.stderr)
        return 1

    if not os.path.isdir(args.project_root):
        print(f'Error: project root not found: {args.project_root}', file=sys.stderr)
        return 1

    try:
        normalize_lcov(args.input, args.output, args.project_root)
        return 0
    except Exception as e:
        print(f'Error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())