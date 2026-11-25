#!/usr/bin/env bash
# Builds and runs the host-side unit tests for src/core (no game or Windows SDK needed).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
"${CXX:-c++}" -std=c++23 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -Isrc tests/core_tests.cpp src/core/*.cpp -o build/host-tests/core_tests
./build/host-tests/core_tests
