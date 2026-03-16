# ── App resources (compiled into the nemo / nemo-plus BINARY) ────────────────
# These must live in the binary, not in libnemo-extension, so that a
# differently-named build (e.g. nemo-plus) uses its own patched versions of
# UI files (preferences glade, etc.) rather than whatever the installed
# libnemo-extension.so happens to contain.
gresources = gnome.compile_resources(
  'nemo-resources', 'nemo-app.gresource.xml',
  source_dir: '.',
  c_name: 'nemo',
  install_header: false,
  install: false
)

# ── Extension resources (compiled into libnemo-extension SHARED LIBRARY) ─────
# Only the single file that libnemo-extension actually needs at runtime.
# Keeping this minimal avoids the shared library carrying UI files that
# belong to the application binary.
extension_gresources = gnome.compile_resources(
  'nemo-extension-resources', 'nemo-extension.gresource.xml',
  source_dir: '.',
  c_name: 'nemo_extension',
  install_header: false,
  install: false
)

layout_editor_gresources = gnome.compile_resources(
  'nemo-action-layout-editor-resources', 'nemo-action-layout-editor.gresource.xml',
  source_dir: '.',
  gresource_bundle: true,
  install: true,
  install_dir: nemoDataPath
)

# Install the preferences glade to NEMO_DATADIR so the C code can load it
# from a file path rather than from a GResource.  This is necessary because
# the OS-installed libnemo-extension.so registers the same GResource path
# (/org/nemo/nemo-file-management-properties.glade) with its own (unpatched)
# copy, and its registration wins over the binary's since shared libraries
# are loaded before main() runs.  Loading from NEMO_DATADIR guarantees each
# build uses its own version of the file.
install_data('nemo-file-management-properties.glade',
  install_dir: nemoDataPath
)
