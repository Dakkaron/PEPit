#!/bin/sh

cmake --build build -j$(nproc) 2>&1 | grep -E "error|Built target pepit_tests" && SDL_VIDEODRIVER=dummy ./build/tests/pepit_tests
