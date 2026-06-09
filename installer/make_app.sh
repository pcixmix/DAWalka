#!/usr/bin/env bash
# =============================================================================
#  DAWalka — build DAWalka.app from the pre-built component
# =============================================================================
#
#  This script packages the freshly-built C++ component and the Python
#  backend into a self-contained DAWalka.app that the user can:
#
#     - double-click to install/uninstall via a Cocoa UI
#     - copy to another Mac and double-click there (the bundle is
#       ad-hoc code-signed, so Gatekeeper doesn't block it — fixes the
#       "bad interpreter: Operation not permitted" error users hit with
#       the .command wrappers when the .command was quarantined)
#
#  Layout produced:
#
#     DAWalka.app/
#     ├── Contents/
#     │   ├── Info.plist
#     │   ├── MacOS/DAWalka               # compiled Cocoa app
#     │   └── Resources/
#     │       ├── Scripts/
#     │       │   ├── install.sh          # the "Install" button
#     │       │   └── uninstall.sh        # the "Uninstall" button
#     │       ├── component/
#     │       │   └── DAWalka.component   # pre-built AUv2
#     │       └── python_backend/         # embedded by the C++ build
#
#  Requirements: build.sh has been run first (so build/AU/DAWalka.component
#  exists) and clang is available (it's in Xcode Command Line Tools, which
#  are required by build.sh anyway).
#
#  Usage:
#     ./installer/make_app.sh
#     ./installer/make_app.sh --rebuild   # blow away DAWalka.app first
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_NAME="DAWalka"
APP_BUNDLE="$PROJECT_ROOT/$APP_NAME.app"

# ─── flags ─────────────────────────────────────────────────────────────────
DO_REBUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild|-r)  DO_REBUILD=1; shift;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *)  echo "Unknown flag: $1" >&2; exit 2;;
    esac
done

# ─── preconditions ─────────────────────────────────────────────────────────
if [[ ! -d "$PROJECT_ROOT/build/AU/DAWalka.component" ]]; then
    echo "ERROR: build/AU/DAWalka.component not found." >&2
    echo "       Run ./build.sh first to compile the C++ plugin." >&2
    exit 1
fi

if ! command -v clang >/dev/null 2>&1; then
    echo "ERROR: clang not found.  Install Xcode Command Line Tools:" >&2
    echo "       xcode-select --install" >&2
    exit 1
fi

if [[ ! -d "$PROJECT_ROOT/python_backend" ]]; then
    echo "ERROR: python_backend/ not found.  Make sure you're in the project root." >&2
    exit 1
fi

# ─── clean previous build ──────────────────────────────────────────────────
if [[ -d "$APP_BUNDLE" ]]; then
    if [[ "$DO_REBUILD" -eq 1 ]]; then
        echo "==> Removing previous DAWalka.app"
        rm -rf "$APP_BUNDLE"
    else
        echo "DAWalka.app already exists.  Use --rebuild to overwrite." >&2
        exit 1
    fi
fi

# ─── bundle skeleton ───────────────────────────────────────────────────────
echo "==> Building DAWalka.app"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources/Scripts"
mkdir -p "$APP_BUNDLE/Contents/Resources/component"
mkdir -p "$APP_BUNDLE/Contents/Resources/vst3"
mkdir -p "$APP_BUNDLE/Contents/Resources/python_backend"

# ─── 1. Compile the Cocoa UI ──────────────────────────────────────────────
echo "==> Compiling Cocoa UI (clang -framework Cocoa)"
if ! clang -framework Cocoa \
        -mmacosx-version-min=14.0 \
        -O2 \
        -o "$APP_BUNDLE/Contents/MacOS/$APP_NAME" \
        "$SCRIPT_DIR/main.m"; then
    echo "ERROR: clang failed" >&2
    exit 1
fi
echo "    $(du -h "$APP_BUNDLE/Contents/MacOS/$APP_NAME" | cut -f1)  $APP_BUNDLE/Contents/MacOS/$APP_NAME"

# ─── 2. Info.plist ─────────────────────────────────────────────────────────
cp "$SCRIPT_DIR/Info.plist" "$APP_BUNDLE/Contents/Info.plist"
echo "    Info.plist"

# ─── 3. Scripts (the actual installer / uninstaller) ───────────────────────
cp "$SCRIPT_DIR/install.sh"   "$APP_BUNDLE/Contents/Resources/Scripts/install.sh"
cp "$PROJECT_ROOT/uninstall.sh" "$APP_BUNDLE/Contents/Resources/Scripts/uninstall.sh"
chmod +x "$APP_BUNDLE/Contents/Resources/Scripts/"*.sh
echo "    Scripts/install.sh  +  Scripts/uninstall.sh"

# ─── 4. Pre-built component ────────────────────────────────────────────────
cp -R "$PROJECT_ROOT/build/AU/DAWalka.component" \
      "$APP_BUNDLE/Contents/Resources/component/"
echo "    component/DAWalka.component  ($(du -sh "$APP_BUNDLE/Contents/Resources/component/DAWalka.component" | cut -f1))"

if [[ -d "$PROJECT_ROOT/build/VST3/DAWalka.vst3" ]]; then
    cp -R "$PROJECT_ROOT/build/VST3/DAWalka.vst3" \
          "$APP_BUNDLE/Contents/Resources/vst3/"
    echo "    vst3/DAWalka.vst3  ($(du -sh "$APP_BUNDLE/Contents/Resources/vst3/DAWalka.vst3" | cut -f1))"
else
    echo "WARNING: build/VST3/DAWalka.vst3 not found — VST3 will not be bundled" >&2
fi

# ─── 5. Python backend (everything except vendor/ which is huge) ──────────
# Use rsync if available so we can exclude vendor/ and __pycache__/;
# fall back to cp -R otherwise.
if command -v rsync >/dev/null 2>&1; then
    rsync -a --exclude='vendor/' \
               --exclude='__pycache__/' \
               --exclude='*.pyc' \
               --exclude='venv/' \
               --exclude='.venv/' \
               --exclude='*.egg-info/' \
               --exclude='.mypy_cache/' \
               --exclude='.pytest_cache/' \
          "$PROJECT_ROOT/python_backend/" \
          "$APP_BUNDLE/Contents/Resources/python_backend/"
else
    cp -R "$PROJECT_ROOT/python_backend"/{server.py,requirements.txt,*.py} \
          "$APP_BUNDLE/Contents/Resources/python_backend/" 2>/dev/null || true
    [[ -d "$PROJECT_ROOT/python_backend/templates" ]] && \
        cp -R "$PROJECT_ROOT/python_backend/templates" \
              "$APP_BUNDLE/Contents/Resources/python_backend/" || true
fi
echo "    python_backend/  ($(du -sh "$APP_BUNDLE/Contents/Resources/python_backend" | cut -f1))"

# ─── 6. README (optional, for the README button) ──────────────────────────
[[ -f "$PROJECT_ROOT/README.md" ]] && \
    cp "$PROJECT_ROOT/README.md" "$APP_BUNDLE/Contents/Resources/README.md" && \
    echo "    README.md"

# ─── 7. App icon (icon.png → AppIcon.icns) ────────────────────────────────
# Build a real macOS .icns bundle so the .app gets its icon in the
# Dock and Finder (and as the doc icon when the installer window is
# minimised).  macOS needs the multi-resolution .icns format — a plain
# 1024x1024 PNG won't be used as the app icon.  Skip with a warning
# if icon.png is missing or sips/iconutil aren't available, so the
# .app still builds (it'll just get the default generic app icon).
if [[ -f "$PROJECT_ROOT/icon.png" ]] && \
   command -v sips >/dev/null 2>&1 && \
   command -v iconutil >/dev/null 2>&1; then
    echo "==> Building AppIcon.icns from icon.png"
    ICONSET_DIR=$(mktemp -d)/AppIcon.iconset
    mkdir -p "$ICONSET_DIR"
    # The 10 sizes macOS expects: 16/32/64/128/256/512/1024, with @2x
    # variants for the retina (32, 64, 256, 512, 1024).  sips -z
    # rescales the source PNG to each size, preserving aspect ratio
    # and producing sRGB PNGs that iconutil accepts.
    for spec in \
        "16:icon_16x16.png" \
        "32:icon_16x16@2x.png" \
        "32:icon_32x32.png" \
        "64:icon_32x32@2x.png" \
        "128:icon_128x128.png" \
        "256:icon_128x128@2x.png" \
        "256:icon_256x256.png" \
        "512:icon_256x256@2x.png" \
        "512:icon_512x512.png" \
        "1024:icon_512x512@2x.png"; do
        size="${spec%%:*}"
        name="${spec##*:}"
        sips -z "$size" "$size" "$PROJECT_ROOT/icon.png" \
             --out "$ICONSET_DIR/$name" >/dev/null
    done
    if iconutil -c icns "$ICONSET_DIR" \
                -o "$APP_BUNDLE/Contents/Resources/AppIcon.icns" 2>/dev/null; then
        echo "    AppIcon.icns  ($(du -h "$APP_BUNDLE/Contents/Resources/AppIcon.icns" | cut -f1))"
    else
        echo "WARNING: iconutil failed; the .app will use the default icon." >&2
        rm -f "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
    fi
    rm -rf "$(dirname "$ICONSET_DIR")"
else
    echo "    (icon.png missing or sips/iconutil unavailable — using default icon)"
fi

# ─── 8. Ad-hoc code sign ───────────────────────────────────────────────────
# This is what solves the Gatekeeper "bad interpreter" error.  Ad-hoc
# signing isn't a real identity, but it's enough to mark the bundle
# as not-quarantined on a Mac that already trusts the local user.
# For real distribution you'd swap "-" for a Developer ID, but for the
# common case (developer copies the .app to /Applications on their own
# Mac) this is fine.
echo "==> Ad-hoc code signing"
if ! codesign --force --deep --sign - "$APP_BUNDLE" 2>&1; then
    echo "WARNING: codesign failed; the .app will still work but may need" >&2
    echo "         'xattr -dr com.apple.quarantine' on first launch." >&2
fi

# ─── summary ───────────────────────────────────────────────────────────────
echo
echo "============================================================"
echo "  Built: $APP_BUNDLE"
echo "  Size:  $(du -sh "$APP_BUNDLE" | cut -f1)"
echo
echo "  Double-click in Finder, or:"
echo "      open '$APP_BUNDLE'"
echo
echo "  To distribute: zip the .app, send it.  Recipients can"
echo "  double-click and click \xE2\x9C\x95 Install."
echo "============================================================"
