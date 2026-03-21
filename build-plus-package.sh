#!/bin/bash
# build-package.sh — build both nemo and nemo-plus .deb packages
#
# Usage:
#   ./build-package.sh          # build both packages (default)
#   ./build-package.sh nemo     # build standard nemo only
#   ./build-package.sh nemo-plus # build nemo-plus only
#
# Output:
#   nemo_{VERSION}-{RELEASE}_amd64.deb
#   nemo-plus_{VERSION}-{RELEASE}_amd64.deb
#
# Both packages are co-installable:
#   - nemo       → /usr/bin/nemo,        /usr/share/nemo,        D-Bus: org.Nemo
#   - nemo-plus  → /usr/bin/nemo-plus,   /usr/share/nemo-plus,   D-Bus: org.NemoPlus
#
# The shared libnemo-extension library is included in both packages.
# Install nemo first (it owns the library), then nemo-plus.
set -e

VERSION="6.6.3"
RELEASE="6"
ARCH="amd64"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── pre-flight source verification ───────────────────────────────────────────
# Confirm that all required source patches are present before spending time
# building. Each check tests for a string that only exists in our patched
# version of that file — if any check fails the build is aborted immediately
# with a clear message pointing at which file is missing its changes.
verify_sources() {
    local ok=true

    check() {
        local file="$1" token="$2" description="$3"
        if ! grep -q "$token" "${SCRIPT_DIR}/$file" 2>/dev/null; then
            echo "  MISSING PATCH: $file"
            echo "    Expected: $description"
            ok=false
        fi
    }

    echo "Verifying source patches are applied..."

    # Core dual-pane feature keys in the GSettings schema
    check "libnemo-private/org.nemo.gschema.xml" \
          "dual-pane-vertical-split" \
          "new dual-pane GSettings keys"

    # New preference key #define in global-preferences header
    check "libnemo-private/nemo-global-preferences.h" \
          "NEMO_PREFERENCES_DUAL_PANE_SEPARATE_STATUSBAR" \
          "dual-pane preference key defines"

    # config.h template must have APP_TITLE mesondefine
    check "config.h.meson.in" \
          "mesondefine APP_TITLE" \
          "APP_TITLE/APP_NAME mesondefine entries"

    # Preferences UI glade must contain all 4 new checkboxes
    check "gresources/nemo-file-management-properties.glade" \
          "dual_pane_vertical_split_checkbutton" \
          "dual-pane checkboxes in preferences glade"

    check "gresources/nemo-file-management-properties.glade" \
          "dual_pane_separate_statusbar_checkbutton" \
          "separate-statusbar checkbox in preferences glade"

    # Preferences C code must bind all new checkboxes
    check "src/nemo-file-management-properties.c" \
          "NEMO_PREFERENCES_DUAL_PANE_SEPARATE_STATUSBAR" \
          "separate-statusbar preference binding in C"

    # Preferences glade must be loaded from NEMO_DATADIR file, not GResource.
    # The OS libnemo-extension.so registers the same GResource path with the
    # old (unpatched) glade, and its registration wins over the binary's.
    check "src/nemo-file-management-properties.c" \
          "gtk_builder_add_from_file" \
          "preferences glade loaded from file not GResource"

    # Window code must have per-pane statusbar support
    check "src/nemo-window.c" \
          "nemo_window_set_up_per_pane_statusbars" \
          "per-pane statusbar functions in nemo-window.c"

    # Window private header must have the new fields
    check "src/nemo-window-private.h" \
          "nemo_status_bar2" \
          "per-pane statusbar fields in nemo-window-private.h"

    # Statusbar must have locked_pane / new_for_pane support
    check "src/nemo-statusbar.c" \
          "nemo_status_bar_new_for_pane" \
          "per-pane statusbar constructor in nemo-statusbar.c"

    # prgname must use APP_NAME not hardcoded "nemo" — fixes panel grouping
    # and ensures preferences dialog opens from nemo-plus itself, not stock nemo
    check "src/nemo-main.c" \
          "g_set_prgname (APP_NAME)" \
          "g_set_prgname uses APP_NAME in nemo-main.c"

    # Open-as-root must spawn APP_NAME not hardcoded nemo
    check "src/nemo-view.c" \
          'argv\[1\] = (gchar \*)APP_NAME' \
          "pkexec open-as-root uses APP_NAME in nemo-view.c"

    # Polkit policy must authorise APP_NAME binary
    check "data/org.nemo.root.policy.in" \
          "@APP_NAME@" \
          "polkit policy uses @APP_NAME@ for authorised binary"

    # Desktop file must have StartupWMClass so taskbar groups correctly
    check "data/nemo.desktop.in" \
          "StartupWMClass=@APP_NAME@" \
          "StartupWMClass set in nemo.desktop.in"

    # gresources must be split so app UI files are in the binary not libnemo-extension.
    # Without this the installed nemo-plus loads the OS stock libnemo-extension.so
    # which has the old glade baked in, causing the preferences dialog to show
    # the old layout without our new dual-pane options.
    check "gresources/nemo-app.gresource.xml" \
          "nemo-file-management-properties.glade" \
          "app gresource XML exists with preferences glade"

    check "gresources/nemo-extension.gresource.xml" \
          "nemo-desktop-preferences.glade" \
          "extension gresource XML exists (minimal)"

    check "gresources/meson.build" \
          "nemo-app.gresource.xml" \
          "gresources/meson.build references split gresource XMLs"

    check "src/meson.build" \
          "gresources," \
          "src/meson.build includes gresources in binary sources"

    check "libnemo-extension/meson.build" \
          "extension_gresources" \
          "libnemo-extension uses extension_gresources not full gresources"

    # App-name option in meson
    check "meson_options.txt" \
          "app_name" \
          "app_name option in meson_options.txt"

    # App-name derivation in root meson.build
    check "meson.build" \
          "app_display_suffix" \
          "app_display_suffix in meson.build"

    # Desktop file suffix script
    check "data/append_desktop_name_suffix.py" \
          "Desktop Entry" \
          "desktop name suffix script"

    if [ "$ok" = false ]; then
        echo ""
        echo "ERROR: One or more source files are missing required patches."
        echo "Apply the complete patch set from nemo_complete_all_changes.zip"
        echo "to this source tree before running build-package.sh."
        exit 1
    fi

    echo "  All source patches verified OK."
}

verify_sources

# ── which packages to build ──────────────────────────────────────────────────
BUILD_TARGET="${1:-both}"   # "nemo", "nemo-plus", or "both" (default)

case "$BUILD_TARGET" in
  nemo|nemo-plus|both) ;;
  *)
    echo "Usage: $0 [nemo|nemo-plus|both]" >&2
    exit 1
    ;;
esac

# ── shared runtime dependencies ───────────────────────────────────────────────
COMMON_DEPENDS="libc6, libgtk-3-0 (>= 3.10), libglib2.0-0 (>= 2.45.7),
 libcinnamon-desktop4, libxapp1, libjson-glib-1.0-0,
 cinnamon-desktop-data, cinnamon-l10n,
 desktop-file-utils, gsettings-desktop-schemas, gvfs,
 shared-mime-info, xapp-symbolic-icons"

# ─────────────────────────────────────────────────────────────────────────────
# build_package APP_NAME DESCRIPTION [EXTRA_DEPENDS] [EXTRA_CONTROL_FIELDS]
#
#   APP_NAME             — "nemo" or "nemo-plus"
#   DESCRIPTION          — one-line package description
#   EXTRA_DEPENDS        — optional additional Depends entries (comma-prefixed)
#   EXTRA_CONTROL_FIELDS — optional extra lines appended to the control file
# ─────────────────────────────────────────────────────────────────────────────
build_package() {
    local app_name="$1"
    local description="$2"
    local extra_depends="${3:-}"
    local extra_fields="${4:-}"

    local build_dir="${SCRIPT_DIR}/build-${app_name}"
    local pkg_dir="${SCRIPT_DIR}/pkg-${app_name}"
    local deb_file="${SCRIPT_DIR}/${app_name}_${VERSION}-${RELEASE}_${ARCH}.deb"

    echo ""
    echo "══════════════════════════════════════════════════════════════════════"
    echo "  Building package: ${app_name} ${VERSION}-${RELEASE}"
    echo "══════════════════════════════════════════════════════════════════════"

    # ── configure & compile ──────────────────────────────────────────────────
    rm -rf "${build_dir}"
    meson setup "${build_dir}" \
        --buildtype=release    \
        --prefix=/usr          \
        --libdir=/usr/lib/x86_64-linux-gnu \
        -D app_name="${app_name}"

    ninja -C "${build_dir}"

    # ── stage install ────────────────────────────────────────────────────────
    rm -rf "${pkg_dir}"
    mkdir -p "${pkg_dir}"
    DESTDIR="${pkg_dir}" ninja -C "${build_dir}" install

    # ── DEBIAN/control ───────────────────────────────────────────────────────
    mkdir -p "${pkg_dir}/DEBIAN"

    local depends="${COMMON_DEPENDS}${extra_depends}"

    cat > "${pkg_dir}/DEBIAN/control" << EOF
Package: ${app_name}
Version: ${VERSION}-${RELEASE}
Architecture: ${ARCH}
Maintainer: Nemo User <nemo@localhost>
Depends: ${depends}
Description: ${description}
${extra_fields}
EOF

    # ── permissions & build deb ──────────────────────────────────────────────
    find "${pkg_dir}" -type d | xargs chmod 755

    dpkg-deb --build "${pkg_dir}" "${deb_file}"

    echo ""
    echo "  Built: ${deb_file}"

    # ── post-build binary verification ───────────────────────────────────────
    # Confirm the compiled binary actually contains the dual-pane feature code.
    # If this fails it means the binary was compiled from unpatched source even
    # though the source files look correct (e.g. a stale build cache was used).
    local binary="${pkg_dir}/usr/bin/${app_name}"
    if [ -f "${binary}" ]; then
        echo "  Verifying compiled binary contains dual-pane feature..."
        local missing_in_binary=false
        for token in \
            "dual-pane-vertical-split" \
            "dual_pane_separate_statusbar_checkbutton" \
            "nemo_window_set_up_per_pane_statusbars"; do
            if ! strings "${binary}" | grep -q "${token}"; then
                echo "  WARNING: binary missing expected string: ${token}"
                missing_in_binary=true
            fi
        done
        if [ "${missing_in_binary}" = true ]; then
            echo ""
            echo "  ERROR: The compiled binary is missing dual-pane feature strings."
            echo "  This means the binary was compiled from unpatched source."
            echo "  Check that all files from nemo_complete_all_changes.zip are"
            echo "  in place and re-run this script."
            exit 1
        else
            echo "  Binary verification passed."
        fi
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# Package definitions
# ─────────────────────────────────────────────────────────────────────────────

build_nemo() {
    build_package \
        "nemo" \
        "Nemo file manager and graphical shell for Cinnamon" \
        "" \
        " This is the standard Nemo build. It co-installs alongside nemo-plus."
}

build_nemo_plus() {
    local app_name="nemo-plus"
    local description="Nemo file manager with dual-pane and preview-pane enhancements"
    local build_dir="${SCRIPT_DIR}/build-${app_name}"
    local pkg_dir="${SCRIPT_DIR}/pkg-${app_name}"
    local deb_file="${SCRIPT_DIR}/${app_name}_${VERSION}-${RELEASE}_${ARCH}.deb"

    echo ""
    echo "══════════════════════════════════════════════════════════════════════"
    echo "  Building package: ${app_name} ${VERSION}-${RELEASE}"
    echo "══════════════════════════════════════════════════════════════════════"

    rm -rf "${build_dir}"
    meson setup "${build_dir}" \
        --buildtype=release    \
        --prefix=/usr          \
        --libdir=/usr/lib/x86_64-linux-gnu \
        -D app_name="${app_name}"

    ninja -C "${build_dir}"

    rm -rf "${pkg_dir}"
    mkdir -p "${pkg_dir}"
    DESTDIR="${pkg_dir}" ninja -C "${build_dir}" install

    mkdir -p "${pkg_dir}/DEBIAN"

    local depends="${COMMON_DEPENDS}, libnemo-extension1, gir1.2-nemo-3.0"

    cat > "${pkg_dir}/DEBIAN/control" << EOF
Package: ${app_name}
Version: ${VERSION}-${RELEASE}
Architecture: ${ARCH}
Maintainer: cdmdotnet Limited <nemo@localhost>
Depends: ${depends}
Replaces: nemo-data
Breaks: nemo-data (<< ${VERSION})
Description: ${description}
 .
 Nemo-Plus is a fork of nemo with additional dual-pane/preview-pane features
 similar to those of xplorer² on windows. This was done as nemo,
 being part of Linux Mint, has a strong reputation for being one of (if not the)
 best distros for those wishing to move from Windows to Linux.
 This has similarities to 4pane, which we would have stuck with, however found
 its layout/aesthetics a bit dated and out of place in Cinnamon along with
 cross app integration for features like copy-paste/drag-and-drop of files
 to be inconsistent when compared to nemo.
 .
 Co-installable with the standard/stock nemo package — uses separate binary
 (/usr/bin/nemo-plus), data directory (/usr/share/nemo-plus), and
 D-Bus name (org.NemoPlus).
 Installs an updated GSettings schema (strict superset of nemo-data's)
 required for the new dual-pane preference keys.
EOF

    find "${pkg_dir}" -type d | xargs chmod 755

    # postinst: recompile GSettings schemas after the updated schema file lands
    cat > "${pkg_dir}/DEBIAN/postinst" << 'POSTINST'
#!/bin/sh
set -e
if which glib-compile-schemas >/dev/null 2>&1; then
    glib-compile-schemas /usr/share/glib-2.0/schemas/ || true
fi
if which gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor/ || true
fi
POSTINST
    chmod 755 "${pkg_dir}/DEBIAN/postinst"

    # postrm: recompile schemas on removal too (restores stock nemo schema)
    cat > "${pkg_dir}/DEBIAN/postrm" << 'POSTRM'
#!/bin/sh
set -e
if which glib-compile-schemas >/dev/null 2>&1; then
    glib-compile-schemas /usr/share/glib-2.0/schemas/ || true
fi
if which gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor/ || true
fi
POSTRM
    chmod 755 "${pkg_dir}/DEBIAN/postrm"

    # ── bundle the preview-pane SVG icon ─────────────────────────────────────
    # nemo-plus may be installed alongside the OS stock nemo (not our patched
    # nemo package), so we cannot rely on the nemo package to provide this icon.
    # Copy it directly into the hicolor icon tree inside this package so it is
    # always present regardless of which nemo variant is installed.
    local svg_src="${SCRIPT_DIR}/data/icons/hicolor/actions/scalable/nemo-preview-pane-symbolic.svg"
    local svg_dst="${pkg_dir}/usr/share/icons/hicolor/scalable/actions"
    if [ -f "${svg_src}" ]; then
        mkdir -p "${svg_dst}"
        cp "${svg_src}" "${svg_dst}/nemo-preview-pane-symbolic.svg"
        echo "  Bundled nemo-preview-pane-symbolic.svg into package."
    else
        echo "  WARNING: SVG source not found: ${svg_src}" >&2
        echo "  The preview pane toolbar icon will be missing in the installed package." >&2
    fi

    dpkg-deb --build "${pkg_dir}" "${deb_file}"

    echo ""
    echo "  Built: ${deb_file}"

    # ── post-build binary verification ───────────────────────────────────────
    echo "  Verifying compiled binary contains dual-pane feature..."
    local missing_in_binary=false
    for token in \
        "dual-pane-vertical-split" \
        "dual_pane_separate_statusbar_checkbutton" \
        "nemo_window_set_up_per_pane_statusbars"; do
        if ! strings "${pkg_dir}/usr/bin/${app_name}" | grep -q "${token}"; then
            echo "  WARNING: binary missing expected string: ${token}"
            missing_in_binary=true
        fi
    done
    if [ "${missing_in_binary}" = true ]; then
        echo ""
        echo "  ERROR: The compiled binary is missing dual-pane feature strings."
        echo "  The binary was compiled from unpatched source — ensure all files"
        echo "  from nemo_complete_all_changes.zip are applied and rebuild."
        exit 1
    else
        echo "  Binary verification passed."
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

case "$BUILD_TARGET" in
    nemo)
        build_nemo
        ;;
    nemo-plus)
        build_nemo_plus
        ;;
    both)
        build_nemo_plus
        build_nemo
        ;;
esac

echo ""
echo "══════════════════════════════════════════════════════════════════════"
echo "  Done."
case "$BUILD_TARGET" in
    both)
        echo "  nemo-plus_${VERSION}-${RELEASE}_${ARCH}.deb"
        echo "  nemo_${VERSION}-${RELEASE}_${ARCH}.deb"
        echo ""
        echo "  Install order (nemo owns libnemo-extension):"
        echo "    sudo dpkg -i nemo_${VERSION}-${RELEASE}_${ARCH}.deb"
        echo "    sudo dpkg -i nemo-plus_${VERSION}-${RELEASE}_${ARCH}.deb"
        ;;
    *)
        echo "  ${BUILD_TARGET}_${VERSION}-${RELEASE}_${ARCH}.deb"
        ;;
esac
echo "══════════════════════════════════════════════════════════════════════"
