project('buzzlocker', 'c')

gnome = import('gnome') # for gresources

cc = meson.get_compiler('c')

sources = [
  'src/auth.c',
  'src/animation.c',
  'src/main.c',
  'src/render.c',
  'src/x11_support.c',
]

dependencies = [
  dependency('x11'),
  dependency('xrandr'),
  dependency('cairo'),
  dependency('librsvg-2.0'),
  dependency('pangocairo'),
  dependency('gio-2.0'),
  cc.find_library('pam', required: true),
  cc.find_library('pthread', required: true),
]

# Resources
resources = gnome.compile_resources(
  'resources', 
  'resources/buzzsaver.gresource.xml',
  source_dir: 'resources',
  c_name: 'as'
)

executable('auth_buzzlocker',
  sources: sources + resources,
  dependencies: dependencies,
  install: true
)