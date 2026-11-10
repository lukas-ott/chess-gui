#!/usr/bin/env bash
# Builds a self-contained Kiebitz.app and a .dmg that runs on a Mac without Homebrew or Qt.
#
#   1. Builds the release preset (unless --no-build).
#   2. Copies the app and only the Qt plugins it needs into a staging folder.
#   3. Runs macdeployqt, which copies Qt and every library those depend on into the bundle
#      and rewrites their load paths to point inside it.
#   4. Fails if anything in the bundle still loads a library from outside macOS itself.
#   5. Signs the bundle ad hoc (enough to run locally; see "Gatekeeper" below).
#   6. Wraps it in a compressed .dmg with an Applications shortcut.
#
# Usage: scripts/package_macos.sh [--no-build] [build-dir]   (build-dir defaults to build/release)
# Output: <build-dir>/package/Kiebitz.app and <build-dir>/package/Kiebitz-<version>.dmg
#
# Gatekeeper: an ad-hoc signature is not a Developer ID signature, so on another Mac the first
# launch needs right-click → Open. Signing with a Developer ID and notarising needs a paid
# Apple Developer account and isn't done here.
set -euo pipefail

BUILD=1
if [[ "${1:-}" == "--no-build" ]]; then
    BUILD=0
    shift
fi
BUILD_DIR="${1:-build/release}"
cd "$(git rev-parse --show-toplevel)"

# Plugins a Qt Widgets app on macOS needs. macdeployqt would otherwise copy every plugin Qt
# has installed (virtual keyboard, PDF, QML, ...), which pulls in unused frameworks.
# Add to this list when the app starts using more (e.g. imageformats/libqsvg for SVG pieces).
PLUGINS=(
    platforms/libqcocoa.dylib
    styles/libqmacstyle.dylib
    tls/libqsecuretransportbackend.dylib # HTTPS (Stockfish download, Lichess cloud) via macOS
)

QTPATHS="${QTPATHS:-$(command -v qtpaths6 || command -v qtpaths)}"
QT_PLUGINS="$("$QTPATHS" --query QT_INSTALL_PLUGINS)"
MACDEPLOYQT="$("$QTPATHS" --query QT_INSTALL_BINS)/macdeployqt"

if ((BUILD)); then
    preset="$(basename "$BUILD_DIR")"
    cmake --preset "$preset"
    cmake --build --preset "$preset"
fi

SRC_APP="$BUILD_DIR/app/Kiebitz.app"
OUT="$BUILD_DIR/package"
APP="$OUT/Kiebitz.app"
[[ -d "$SRC_APP" ]] || { echo "No app at $SRC_APP" >&2; exit 1; }

echo "==> Staging $APP"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$SRC_APP" "$APP"

executables=()
for plugin in "${PLUGINS[@]}"; do
    mkdir -p "$APP/Contents/PlugIns/$(dirname "$plugin")"
    cp "$QT_PLUGINS/$plugin" "$APP/Contents/PlugIns/$plugin"
    executables+=("-executable=$APP/Contents/PlugIns/$plugin")
done

echo "==> Bundling Qt and its dependencies (macdeployqt)"
"$MACDEPLOYQT" "$APP" -no-plugins "${executables[@]}"

echo "==> Checking the bundle is self-contained"
outside=0
while IFS= read -r -d '' file; do
    if file -b "$file" | grep -q 'Mach-O'; then
        # Everything must load from inside the bundle (@rpath, @executable_path, @loader_path)
        # or from macOS itself (/usr/lib, /System). A library's own install name (otool -D) is
        # listed by otool -L too but isn't something it loads, so it's skipped.
        own_id="$(otool -D "$file" | tail -n +2)"
        bad="$(otool -L "$file" | tail -n +2 | awk '{print $1}' \
            | grep -vxF -e "${own_id:-@none}" \
            | grep -vE '^(@rpath|@executable_path|@loader_path|/usr/lib/|/System/)' || true)"
        if [[ -n "$bad" ]]; then
            echo "  ${file#"$APP"/} loads from outside the bundle:" >&2
            echo "${bad//$'\n'/$'\n'    }" | sed 's/^/    /' >&2
            outside=1
        fi
    fi
done < <(find "$APP" -type f -print0)
if ((outside)); then
    echo "FAIL: the bundle depends on libraries outside it" >&2
    exit 1
fi
echo "  OK"

echo "==> Signing (ad hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "==> Smoke test"
KIEBITZ_STARTUP_EXIT=1 "$APP/Contents/MacOS/Kiebitz"

VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")"
DMG="$OUT/Kiebitz-$VERSION.dmg"
echo "==> Creating $DMG"
dmg_root="$(mktemp -d)"
trap 'rm -rf "$dmg_root"' EXIT
cp -R "$APP" "$dmg_root/"
ln -s /Applications "$dmg_root/Applications"
hdiutil create -quiet -volname "Kiebitz $VERSION" -srcfolder "$dmg_root" -format UDZO -ov "$DMG"

echo
echo "App: $APP ($(du -sh "$APP" | cut -f1))"
echo "DMG: $DMG ($(du -sh "$DMG" | cut -f1))"
