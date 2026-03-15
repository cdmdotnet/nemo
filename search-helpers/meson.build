libgsf = dependency('libgsf-1', required: true)

mso_to_txt_sources = [
  'nemo-mso-to-txt.c'
]

mso_to_txt = executable('nemo-mso-to-txt',
  mso_to_txt_sources,
  dependencies: [libgsf, gio, glib],
  install: app_name == 'nemo'
)

ppt_to_txt_sources = [
  'nemo-ppt-to-txt.c'
]

ppt_to_txt = executable('nemo-ppt-to-txt',
  ppt_to_txt_sources,
  dependencies: [gio, glib],
  install: app_name == 'nemo'
)

# These script helpers and their search-helper descriptors are only installed
# as part of the base 'nemo' package.  A differently-named build (e.g.
# 'nemo-plus') skips them because the base nemo package already provides them
# and they would otherwise overwrite each other in /usr/bin.
if app_name == 'nemo'
  install_data(
      ['nemo-xls-to-txt', 'nemo-odf-to-txt'],
      install_dir: join_paths(get_option('prefix'), get_option('bindir')),
      install_mode: 'rwxr-xr-x'
  )

  install_data(
      'epub2text.nemo_search_helper',
      'mso.nemo_search_helper',
      'mso-ppt.nemo_search_helper',
      'mso-xls.nemo_search_helper',
      'odf.nemo_search_helper',
      install_dir: join_paths(nemoDataPath, 'search-helpers')
  )

  install_data(
      'nemo-epub2text',
      install_dir: get_option('bindir')
  )
endif

subdir('third-party')