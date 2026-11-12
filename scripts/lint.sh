#!/usr/bin/env bash
# Checks formatting (clang-format) and lint rules (clang-tidy) on every C++ file in the repo,
# including new files not yet added to git (but not ignored ones like build/).
# Fails on any difference or warning. CI runs this too.
#
# Usage: scripts/lint.sh [build-dir] [--changed]
#   build-dir must be configured and built (clang-tidy needs its compile_commands.json and the
#   files Qt generates). Defaults to build/dev.
#   --changed: clang-tidy only the files changed since the last commit (and new ones), plus
#   the .cpp files that include a changed header. Much faster while working; CI checks all.
#   Files the build doesn't compile (e.g. bench/ without KIEBITZ_BUILD_BENCH) are skipped.
#
# Fix formatting in place with:
#   git ls-files -z --cached --others --exclude-standard '*.cpp' '*.hpp' | xargs -0 clang-format -i
set -euo pipefail

BUILD_DIR="build/dev"
CHANGED=0
for arg in "$@"; do
    case "$arg" in
        --changed) CHANGED=1 ;;
        *) BUILD_DIR="$arg" ;;
    esac
done
cd "$(git rev-parse --show-toplevel)"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "No compile_commands.json in $BUILD_DIR. Build it first, e.g.: cmake --workflow --preset dev" >&2
    exit 1
fi

echo "clang-format: $(clang-format --version)"
git ls-files -z --cached --others --exclude-standard '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
echo "Formatting OK"

# A clang-tidy that isn't Apple's or Homebrew's (e.g. from pip) can't find the macOS SDK
# on its own, so pass it explicitly.
extra_args=()
if [[ "$(uname)" == "Darwin" ]]; then
    extra_args=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")
fi

echo "clang-tidy: $(clang-tidy --version | grep -i version)"
# The .cpp files to check: those the build compiles (in compile_commands.json) ...
all_files="$(git ls-files --cached --others --exclude-standard '*.cpp')"
compiled="$(cat "$BUILD_DIR/compile_commands.json")"
files=()
while IFS= read -r file; do
    [[ -n "$file" && "$compiled" == *"$PWD/$file"* ]] && files+=("$file")
done <<<"$all_files"
# ... and with --changed, only the changed ones and those including a changed header.
if [[ "$CHANGED" == 1 ]]; then
    changed="$( (git diff --name-only HEAD; git ls-files --others --exclude-standard) | sort -u)"
    headers=()
    while IFS= read -r file; do
        [[ "$file" == *.hpp ]] && headers+=("$(basename "$file")")
    done <<<"$changed"
    picked=()
    for file in "${files[@]}"; do
        if grep -qxF "$file" <<<"$changed"; then
            picked+=("$file")
            continue
        fi
        for header in "${headers[@]+"${headers[@]}"}"; do
            if grep -q "#include [\"<].*$header[\">]" "$file"; then
                picked+=("$file")
                break
            fi
        done
    done
    files=("${picked[@]+"${picked[@]}"}")
    echo "clang-tidy: ${#files[@]} changed file(s)"
fi
if [[ ${#files[@]} -eq 0 ]]; then
    echo "Lint OK"
    exit 0
fi
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
printf '%s\0' "${files[@]}" | xargs -0 -n 1 -P "$jobs" \
    clang-tidy -p "$BUILD_DIR" --quiet --warnings-as-errors='*' "${extra_args[@]}"
echo "Lint OK"
