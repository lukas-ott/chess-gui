#!/usr/bin/env bash
# Builds app/resources/icon/Kiebitz.icns from Kiebitz.svg (macOS draws the SVG; iconutil packs
# the sizes). The 16 and 32 pt sizes use Kiebitz-small.svg, a simpler drawing that stays readable
# when tiny. Run after changing either SVG, and commit the results.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)/app/resources/icon"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/Kiebitz.iconset"
cat > "$work/render.swift" <<'SWIFT'
import AppKit
let args = CommandLine.arguments
guard let image = NSImage(contentsOfFile: args[1]) else { fatalError("can't read SVG") }
let size = Int(args[3])!
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
image.draw(in: NSRect(x: 0, y: 0, width: size, height: size))
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[2]))
SWIFT
for base in 16 32 128 256 512; do
    art=Kiebitz.svg
    if [ "$base" -le 32 ]; then art=Kiebitz-small.svg; fi
    swift "$work/render.swift" "$art" "$work/Kiebitz.iconset/icon_${base}x${base}.png" "$base"
    swift "$work/render.swift" "$art" "$work/Kiebitz.iconset/icon_${base}x${base}@2x.png" \
        "$((base * 2))"
done
iconutil -c icns "$work/Kiebitz.iconset" -o Kiebitz.icns
# Also a PNG compiled into the app: it sets the Dock icon at run time, so the Dock shows it even
# when macOS still has an older copy of the app (without an icon) in its cache.
cp "$work/Kiebitz.iconset/icon_256x256@2x.png" Kiebitz.png
echo "Wrote $(pwd)/Kiebitz.icns and Kiebitz.png"
