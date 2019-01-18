project('buzzlocker', 'c')

sources = [
  'src/main.c',
  'src/render.c',
  'src/x11_support.c',
]

dependencies = [
  dependency('x11'),
  dependency('cairo'),
  dependency('librsvg-2.0'),
  dependency('pangocairo')
]

executable('buzzlocker',
  sources: sources,
  dependencies: dependencies
)

# Resources
configure_file(
  input: 'resources/logo.svg',
  output: 'logo.svg',
  copy: true
)

configure_file(
  input: 'resources/asterisk.svg',
  output: 'asterisk.svg',
  copy: true
)

