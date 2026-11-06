#!/bin/bash
# Build and run the tests for bfvplus's platform-independent modules with g++ (WSL/Linux).
# The modules listed in PURE must not include windows.h; the DLL build compiles them with MSVC too.
set -u
cd "$(dirname "$0")/.."
PURE=""
mkdir -p tests/build
g++ -std=c++17 -Wall -Wextra -Werror -O1 -Isrc -Itests tests/*.cpp $PURE -o tests/build/bfvplus_tests || exit 1
./tests/build/bfvplus_tests
