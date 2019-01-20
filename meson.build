project('buzzlocker', 'c')

gnome = import('gnome') # for gresources

sources = [
  'src/main.c',
  'src/render.c',
  'src/x11_support.c',
]

dependencies = [
  dependency('x11'),
  dependency('cairo'),
  dependency('librsvg-2.0'),
  dependency('pangocairo'),
  dependency('gio-2.0'),
]

# Resources
resources = gnome.compile_resources(
  'resources', 
  'resources/buzzsaver.gresource.xml',
  source_dir: 'resources',
  c_name: 'as'
)

executable('buzzlocker',
  sources: sources + resources,
  dependencies: dependencies,
  install: true
)
