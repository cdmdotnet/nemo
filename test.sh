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
echo "══ Running dual-pane python test suite ════════════════════════════════════════"

NEMO_BINARY=$(pwd)/build/src/nemo
GSETTINGS_SCHEMA_DIR=$(pwd)/build/test
python3 test/test-dual-pane-integration.py -v