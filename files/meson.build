message('Copying ./files structure')

# gtksourceview language specs are shared nemo infrastructure.
# Only install from the base 'nemo' package.
if app_name == 'nemo'
  install_subdir('usr/share/gtksourceview-2.0',
    install_dir: get_option('datadir'),
  )

  install_subdir('usr/share/gtksourceview-3.0',
    install_dir: get_option('datadir'),
  )
endif

# The nemo data directory content (actions, docs) belongs in nemoDataPath
# which is already app_name-specific (e.g. /usr/share/nemo-plus).
# We install each file individually so the destination directory name is
# controlled by nemoDataPath rather than the hardcoded 'nemo' in the source tree.
install_data(
  'usr/share/nemo/action-info.md',
  'usr/share/nemo/script-info.md',
  install_dir: nemoDataPath,
)

install_data(
  'usr/share/nemo/actions/90_new-launcher.nemo_action',
  'usr/share/nemo/actions/90_new-workspace.nemo_action',
  'usr/share/nemo/actions/91_delete-workspace.nemo_action',
  'usr/share/nemo/actions/92_show-expo.nemo_action',
  'usr/share/nemo/actions/add-desklets.nemo_action',
  'usr/share/nemo/actions/change-background.nemo_action',
  'usr/share/nemo/actions/mount-archive.nemo_action',
  'usr/share/nemo/actions/myaction.py',
  'usr/share/nemo/actions/sample.nemo_action',
  'usr/share/nemo/actions/set-as-background.nemo_action',
  'usr/share/nemo/actions/set-resolution.nemo_action',
  install_dir: join_paths(nemoDataPath, 'actions'),
)
