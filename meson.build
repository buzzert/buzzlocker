project('buzzlocker', 'c', 
  default_options : ['warning_level=3']
)

gnome = import('gnome') # for gresources

cc = meson.get_compiler('c')

sources = [
  'src/auth.c',
  'src/animation.c',
  'src/main.c',
  'src/render.c',
  'src/display_server.c',
  'src/x11_backend.c',
  'src/wayland_backend.c',
]

# Check for Wayland support
wayland_client = dependency('wayland-client', version: '>=1.20.0', required: false)
wayland_protocols = dependency('wayland-protocols', version: '>=1.25', required: false)
wayland_scanner = dependency('wayland-scanner', version: '>=1.15.0', required: false)

dependencies = [
  dependency('x11'),
  dependency('xrandr'),
  dependency('cairo'),
  dependency('librsvg-2.0'),
  dependency('pangocairo'),
  dependency('gio-2.0'),
  dependency('xkbcommon'),
  cc.find_library('pam', required: true),
  cc.find_library('pthread', required: true),
]

# Add Wayland dependencies if available
if wayland_client.found() and wayland_protocols.found() and wayland_scanner.found()
  dependencies += [wayland_client]
  add_project_arguments('-DHAVE_WAYLAND=1', language: 'c')
  
  # Generate Wayland protocol sources
  wayland_scanner_prog = find_program(wayland_scanner.get_variable('wayland_scanner'))
  wayland_protocols_dir = wayland_protocols.get_variable('pkgdatadir')
  
  # ext-session-lock-v1 protocol
  session_lock_xml = wayland_protocols_dir + '/staging/ext-session-lock/ext-session-lock-v1.xml'
  session_lock_header = custom_target('ext-session-lock-v1-client-header',
    input: session_lock_xml,
    output: 'ext-session-lock-v1-client-protocol.h',
    command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'])

  session_lock_code = custom_target('ext-session-lock-v1-client-code',
    input: session_lock_xml,
    output: 'ext-session-lock-v1-client-protocol.c',
    command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'])
  
  sources += [session_lock_code, session_lock_header]
  
  message('Building with Wayland support')
else
  message('Building without Wayland support - some dependencies missing')
endif

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
