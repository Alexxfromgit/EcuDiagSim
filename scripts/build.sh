#!/usr/bin/env bash
# Configure, build, test and run EcuDiagSim on Linux or macOS (CMake presets + Ninja + Qt 6).
#
#   ./scripts/build.sh                  configure + build the 'debug' preset
#   ./scripts/build.sh --test           ... and run ctest
#   ./scripts/build.sh --preset asan --test
#   ./scripts/build.sh --run
#
# Qt is taken from CMAKE_PREFIX_PATH, QT_ROOT_DIR, or the newest ~/Qt/<version>/gcc_64 (macos).
set -euo pipefail

preset=debug
run_tests=0
run_app=0
while [ $# -gt 0 ]; do
    case "$1" in
        --preset) preset="$2"; shift 2 ;;
        --test) run_tests=1; shift ;;
        --run) run_app=1; shift ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "${CMAKE_PREFIX_PATH:-}" ]; then
    if [ -n "${QT_ROOT_DIR:-}" ]; then
        export CMAKE_PREFIX_PATH="$QT_ROOT_DIR"
    else
        for base in "$HOME/Qt" /opt/Qt; do
            qt="$(ls -d "$base"/6.*/gcc_64 "$base"/6.*/macos 2>/dev/null | sort -V | tail -1 || true)"
            if [ -n "$qt" ]; then export CMAKE_PREFIX_PATH="$qt"; break; fi
        done
    fi
fi
if [ -z "${CMAKE_PREFIX_PATH:-}" ]; then
    echo "Qt 6 not found: set CMAKE_PREFIX_PATH or QT_ROOT_DIR to e.g. ~/Qt/6.8.3/gcc_64" >&2
    exit 1
fi

cd "$root"
cmake --preset "$preset"
cmake --build --preset "$preset"
if [ "$run_tests" = 1 ]; then
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ctest --preset "$preset"
fi
if [ "$run_app" = 1 ]; then
    "$root/build/$preset/EcuDiagSim"
fi
