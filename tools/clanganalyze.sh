#!/bin/bash -x

CLANG_ANALYZER="$(find /usr/lib*/clang-analyzer/scan-build/ -name ccc-analyzer -print0 | head -n 1)"
test -f CMakeLists.txt ||
        echo You wanna execute this from the directory containing CMakeLists.txt.

test -f CMakeLists.txt ||
        exit
test -f CMakeCache.txt && rm CMakeCache.txt

BUILDDIR=coveragebuild
mkdir -p "${BUILDDIR}"
cd "${BUILDDIR}"

cmake \
        -DCMAKE_CCC_COMPILER="${CLANG_ANALYZER}" \
        -DCMAKE_CCC_FLAGS="-Wall -Wextra -Wpedantic -pedantic" ..

scan-build --use-analyzer /usr/bin/clang make
