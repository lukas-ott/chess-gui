#!/usr/bin/env bash
# Checks formatting (clang-format) and lint rules (clang-tidy) on every C++ file in git.
# Fails on any difference or warning. CI runs this too.
#
# Usage: scripts/lint.sh [build-dir]
#   build-dir must be configured and built (clang-tidy needs its compile_commands.json and the
#   files Qt generates). Defaults to build/dev.
#
# Fix formatting in place with:
#   git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format -i
set -euo pipefail

BUILD_DIR="${1:-build/dev}"
cd "$(git rev-parse --show-toplevel)"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "No compile_commands.json in $BUILD_DIR. Build it first, e.g.: cmake --workflow --preset dev" >&2
    exit 1
fi

echo "clang-format: $(clang-format --version)"
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
echo "Formatting OK"

# A clang-tidy that isn't Apple's or Homebrew's (e.g. from pip) can't find the macOS SDK
# on its own, so pass it explicitly.
extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
    extra_args=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")
fi

echo "clang-tidy: $(clang-tidy --version | grep -i version)"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
git ls-files -z '*.cpp' | xargs -0 -n 1 -P "$jobs" \
    clang-tidy -p "$BUILD_DIR" --quiet --warnings-as-errors='*' "${extra_args[@]}"
echo "Lint OK"
