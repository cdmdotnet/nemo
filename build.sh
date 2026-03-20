#!/bin/bash
set -e

# ── build ──────────────────────────────────────────────────────────────────
rm -rf build
meson setup --prefix=/usr build
meson compile -C build

# Compile schemas into build/test (where meson puts them for the test suite)
mkdir -p "$(pwd)/build/test"
cp /usr/share/glib-2.0/schemas/org.nemo*.xml     "$(pwd)/build/test/" 2>/dev/null || true
cp /usr/share/glib-2.0/schemas/org.cinnamon*.xml "$(pwd)/build/test/" 2>/dev/null || true
cp libnemo-private/org.nemo.gschema.xml           "$(pwd)/build/test/"
glib-compile-schemas "$(pwd)/build/test/"

# ── tests ───────────────────────────────────────────────────────────────────
# --suite dual-pane runs the schema test only (no display/D-Bus required).
# The schema test verifies our three new GSettings keys exist with correct
# defaults and can be read/written.
#
# Widget layout tests require a full session (display + D-Bus + GVfs) and
# are not part of the automated suite.  To run Nemo itself and verify the
# layout visually, see the "run" section below.
echo ""
echo "══ Running dual-pane test suite ════════════════════════════════════════"
meson test -C build --no-rebuild --suite dual-pane --print-errorlogs

# ── run ─────────────────────────────────────────────────────────────────────
# Reuse the gschemas.compiled produced by the test build target.
SCHEMA_DIR="$(pwd)/build/test"

echo ""
echo "══ Clearing Settings ══════════════════════════════════════════════════════"

## Reset just the nemo window-state schema (sidebar, toolbar, etc.)
#gsettings reset-recursively org.nemo.window-state
#
## Or if you want to reset all nemo settings entirely:
#gsettings reset-recursively org.nemo
#gsettings reset-recursively org.nemo.window-state
#gsettings reset-recursively org.nemo.preferences

echo ""
echo "══ Launching Nemo ══════════════════════════════════════════════════════"

# GTK's icon theme requires hicolor/scalable/actions/ layout, but the source
# tree stores icons as hicolor/actions/scalable/ (corrected by meson install).
# Copy new icons into the build directory with the correct layout so they are
# found when running uninstalled.  This does not touch the source tree.
SOURCE_DATA_DIR="$(pwd)/data"
BUILD_DATA_DIR="$(pwd)/build/data"
mkdir -p "${BUILD_DATA_DIR}/icons/hicolor/scalable/actions"
cp "${SOURCE_DATA_DIR}/icons/hicolor/actions/scalable/nemo-preview-pane-symbolic.svg" \
   "${BUILD_DATA_DIR}/icons/hicolor/scalable/actions/nemo-preview-pane-symbolic.svg"
export XDG_DATA_DIRS="${SOURCE_DATA_DIR}:${BUILD_DATA_DIR}:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR" $(pwd)/build/src/nemo
