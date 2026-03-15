
################################################################################
dataConf = configuration_data()
dataConf.set('VERSION',     meson.project_version())
dataConf.set('APP_NAME',    app_name)
dataConf.set('APP_TITLE',   app_title)
dataConf.set('APP_DBUS_ID', app_dbus_id)

# Step 1: standard @TOKEN@ substitution for autorun and autostart desktop files.
desktopFiles = [
  configure_file(
    input : 'nemo-autorun-software.desktop.in',
    output: app_name + '-autorun-software.desktop',
    configuration: dataConf,
  ),

  configure_file(
    input : 'nemo-autostart.desktop.in',
    output: app_name + '-autostart.desktop',
    configuration: dataConf,
  ),
]

# Step 2: main .desktop file — token substitution first, then append
# app_display_suffix (e.g. '+') to every Name= line in [Desktop Entry] so the
# launcher shows "Files+" for nemo-plus instead of just "Files".
# We use a two-step custom_target: configure_file → python suffix script.
main_desktop_substituted = configure_file(
  input : 'nemo.desktop.in',
  output: app_name + '.desktop.tmp',
  configuration: dataConf,
)

main_desktop_final = custom_target(app_name + '-desktop-file',
  input : main_desktop_substituted,
  output: app_name + '.desktop',
  command: [
    find_program('python3'),
    files('append_desktop_name_suffix.py'),
    '@INPUT@', '@OUTPUT@', app_display_suffix,
  ],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(desktopFiles,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

################################################################################
dbusConf = configuration_data()
dbusConf.set('bindir',      join_paths(get_option('prefix'), get_option('bindir')))
dbusConf.set('APP_NAME',    app_name)
dbusConf.set('APP_DBUS_ID', app_dbus_id)
dbusConf.set('APP_DESKTOP_DBUS_ID', app_desktop_dbus_id)

# The app-specific D-Bus service is always installed (name varies with app_name).
appServiceFile = configure_file(
  input : 'nemo.service.in',
  output: app_dbus_id + '.service',
  configuration: dbusConf,
)

install_data(appServiceFile,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services')
)

# The freedesktop FileManager1 service registration is shared infrastructure —
# only one package on the system can own this file (nemo, nautilus, etc.).
# Only install it from the base 'nemo' package; a differently-named build
# (e.g. 'nemo-plus') skips it to avoid conflicting with whatever package
# already provides org.freedesktop.FileManager1.
if app_name == 'nemo'
  fileManager1ServiceFile = configure_file(
    input : 'nemo.FileManager1.service.in',
    output: 'org.freedesktop.FileManager1.service',
    configuration: dbusConf,
  )

  install_data(fileManager1ServiceFile,
    install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services')
  )
endif

################################################################################
# The mime type, polkit policy and syntax-highlight language specs are shared
# nemo infrastructure.  They are only installed by the base 'nemo' package;
# a differently-named build (e.g. 'nemo-plus') depends on nemo providing them.

if app_name == 'nemo'

mimeFile_translated = custom_target('nemo.xml',
  input : ['nemo.xml.in'],
  output: 'nemo.xml',
  command: ['intltool-merge',
    '--quiet',
    '--xml-style',
    '--utf8',
    po_subdir, '@INPUT@', '@OUTPUT@'
  ],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'mime', 'packages'),
  build_by_default: true,
)

################################################################################
dataConf = configuration_data()
dataConf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

rootPolicyFile = configure_file(
  input : 'org.nemo.root.policy.in',
  output: 'org.nemo.root.policy',
  configuration: dataConf,
)

install_data(rootPolicyFile,
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions')
)

foreach lang_file: ['nemo_action.lang', 'nemo_search_helper.lang']
  foreach sv_dir: ['gtksourceview-2.0', 'gtksourceview-3.0', 'gtksourceview-4']
    install_data(lang_file,
      install_dir: join_paths(get_option('datadir'), sv_dir, 'language-specs')
    )
  endforeach
endforeach

endif # app_name == 'nemo'

################################################################################

subdir('icons')
