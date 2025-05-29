/*
 * wayland_backend.c
 * 
 * Wayland backend implementation using ext-session-lock-v1 protocol
 * Created 2025-05-28 by James Magahern <james@magahern.com>
 */

#include "display_server.h"
#include "render.h"
#include "events.h"

#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

#ifdef HAVE_WAYLAND
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <wayland-client.h>
#include <unistd.h>
#include <string.h>

// This is generated at build time. 
#include "ext-session-lock-v1-client-protocol.h"

#endif

typedef struct {
    bool caps;
    bool control;

    struct xkb_state   *state;
    struct xkb_context *context;
    struct xkb_keymap  *keymap;
} keyboard_state_t;

// Forward declarations
static bool wayland_init(void);
static cairo_surface_t* wayland_acquire_surface(void);
static void wayland_get_display_bounds(unsigned int monitor_num, display_bounds_t *bounds);
static void wayland_poll_events(void *state);
static void wayland_destroy_surface(cairo_surface_t *surface);
static void wayland_cleanup(void);

#ifdef HAVE_WAYLAND

// Wayland globals
static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct wl_surface *surface = NULL;
static struct wl_seat *seat = NULL;
static struct wl_keyboard *keyboard = NULL;
static struct wl_output *output = NULL;
static keyboard_state_t keyboard_state = { 0 };

// Session lock globals
static struct ext_session_lock_manager_v1 *session_lock_manager = NULL;
static struct ext_session_lock_v1 *session_lock = NULL;
static struct ext_session_lock_surface_v1 *lock_surface = NULL;

// Surface and buffer management
static struct wl_buffer *buffer = NULL;
static void *shm_data = NULL;
static int shm_size = 0;
static int surface_width = 1920;  // Default, will be updated
static int surface_height = 1080; // Default, will be updated
static bool surface_configured = false;
static cairo_surface_t *current_cairo_surface = NULL;

// Evidently glibc does not provide a wrapper for this syscall.
static inline int memfd_create(const char *name, unsigned int flags) {
    return syscall(__NR_memfd_create, name, flags);
}

// Session lock listeners
static bool session_is_locked = false;

static void session_lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
    session_is_locked = true;
}

static void session_lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
    session_is_locked = false;
    
    // The session lock has been invalidated
    ext_session_lock_v1_destroy(session_lock);
    session_lock = NULL;
}

static const struct ext_session_lock_v1_listener session_lock_listener = {
    .locked = session_lock_locked,
    .finished = session_lock_finished,
};

// Lock surface listeners  
static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *lock_surface, 
                                   uint32_t serial, uint32_t width, uint32_t height)
{
    surface_width = width;
    surface_height = height;
    surface_configured = true;
    
    ext_session_lock_surface_v1_ack_configure(lock_surface, serial);

    event_t resize_event = (event_t) {
        .type = EVENT_SURFACE_SIZE_CHANGED,
    };

    queue_event(resize_event);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_configure,
};

// Keyboard listeners

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) 
{
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        // Unsupported. 
        close(fd);
        exit(1);
    }

    char *map_shm = mmap(NULL, size - 1, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_shm == MAP_FAILED || map_shm == NULL) {
        close(fd);
        fprintf(stderr, "Unable to initialize keymap shm");
        exit(1);
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_buffer(
        keyboard_state.context, 
        map_shm, 
        size - 1, 
        XKB_KEYMAP_FORMAT_TEXT_V1, 
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );

    munmap(map_shm, size - 1);
    close(fd);

    struct xkb_state *xkb_state = xkb_state_new(keymap);

    xkb_keymap_unref(keyboard_state.keymap);
    xkb_state_unref(keyboard_state.state);

    keyboard_state.keymap = keymap;
    keyboard_state.state = xkb_state;
}
    
static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) 
{
    // Ignore
}
            
static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) 
{
    // Ignore
}
                
static void keyboard_repeat(void *data) 
{
    // TODO: Need to add a timer here and repeat the last seen keyboard event. 
}

static void post_event(event_type_t type, uint32_t codepoint)
{
    event_t event = {
        .codepoint = codepoint,
        .type = type
    };

    queue_event(event);
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) 
{
    const enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard_state.state, key + 8);
    uint32_t keycode = key_state == WL_KEYBOARD_KEY_STATE_PRESSED ? key + 8 : 0;
    
    uint32_t codepoint = xkb_state_key_get_utf32(keyboard_state.state, keycode);
    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        switch (sym) {
            case XKB_KEY_Return: 
                post_event(EVENT_KEYBOARD_RETURN, 0);
                break;
            case XKB_KEY_BackSpace:
                post_event(EVENT_KEYBOARD_BACKSPACE, 0);
                break;
            case XKB_KEY_u:
                if (keyboard_state.control) {
                    // ctrl-u: clear
                    post_event(EVENT_KEYBOARD_CLEAR, 0);
                    break;
                }
            default:
                post_event(EVENT_KEYBOARD_LETTER, codepoint);
                break;
        }
    }
}
                            
static void keyboard_modifiers(
    void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, 
    uint32_t mods_latched, uint32_t mods_locked, uint32_t group) 
{
    if (keyboard_state.state == NULL) {
        return;
    }

    // Update xkb state
    xkb_state_update_mask(keyboard_state.state, mods_depressed, mods_latched, mods_locked, 0, 0, group);

    // Check for caps
    int caps_lock = xkb_state_mod_name_is_active(keyboard_state.state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED);
    if (caps_lock != keyboard_state.caps) {
        keyboard_state.caps = caps_lock;
    }
    
    // Check for control
    keyboard_state.control = xkb_state_mod_name_is_active(keyboard_state.state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) 
{
    // TODO: Once we implement repeat, we'll use the rate/delay plumbed in here to know how to set up our internal timer. 
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

// Seat listeners
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (keyboard) {
            wl_keyboard_destroy(keyboard);
        }

        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    } else {
        fprintf(stderr, "No keyboard capability\n");
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    // Not sure what to do here, I think we don't use this. 
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// Registry listener
static void registry_global(void *data, struct wl_registry *registry,
                          uint32_t id, const char *interface, uint32_t version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        session_lock_manager = wl_registry_bind(registry, id, &ext_session_lock_manager_v1_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, id, &wl_seat_interface, 7);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (!output) { // TODO: need to check for preferred output, this just uses the first one. 
            output = wl_registry_bind(registry, id, &wl_output_interface, 3);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
    // Handle removal of globals
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Shared memory helpers
static int create_shm_file(size_t size)
{
    // Creates a file that lives in ram (ramfs) and has volatile backing storage. 
    int fd = memfd_create("buzzlocker-buffer", MFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

static struct wl_buffer* create_buffer(int width, int height)
{
    int stride = width * 4; // 4 bytes per pixel (ARGB)
    shm_size = stride * height;
    
    int fd = create_shm_file(shm_size);
    if (fd < 0) {
        return NULL;
    }
    
    shm_data = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    
    // Cairo ARGB32 on little-endian is BGRA in memory.
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, shm_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_shm_pool_destroy(pool);
    close(fd);
    
    return buffer;
}

static bool wayland_init(void)
{
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return false;
    }
    
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    
    // First roundtrip to get globals
    wl_display_roundtrip(display);
    
    // Set up seat listener if we found a seat
    if (seat) {
        wl_seat_add_listener(seat, &seat_listener, NULL);
        wl_display_roundtrip(display);
    }
    
    if (!compositor || !shm) {
        fprintf(stderr, "Failed to get required Wayland interfaces\n");
        return false;
    }
    
    if (!session_lock_manager) {
        fprintf(stderr, "Compositor does not support ext-session-lock-v1\n");
        return false;
    }
    
    // Create the session lock
    session_lock = ext_session_lock_manager_v1_lock(session_lock_manager);
    if (!session_lock) {
        fprintf(stderr, "Failed to create session lock\n");
        return false;
    }
    
    ext_session_lock_v1_add_listener(session_lock, &session_lock_listener, NULL);
    keyboard_state.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    
    return true;
}

static cairo_surface_t* wayland_acquire_surface(void)
{
    if (!display || !compositor || !session_lock || !output) {
        return NULL;
    }
    
    // Create Wayland surface
    surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        return NULL;
    }
    
    // Create lock surface for the output
    lock_surface = ext_session_lock_v1_get_lock_surface(session_lock, surface, output);
    if (!lock_surface) {
        wl_surface_destroy(surface);
        return NULL;
    }
    
    ext_session_lock_surface_v1_add_listener(lock_surface, &lock_surface_listener, NULL);

    // Wait for configure event to get the correct size
    wl_display_roundtrip(display);
    
    if (!surface_configured) {
        fprintf(stderr, "Surface not configured after roundtrip\n");
        ext_session_lock_surface_v1_destroy(lock_surface);
        wl_surface_destroy(surface);
        return NULL;
    }
    
    // Create shared memory buffer  
    buffer = create_buffer(surface_width, surface_height);
    if (!buffer) {
        ext_session_lock_surface_v1_destroy(lock_surface);
        wl_surface_destroy(surface);
        return NULL;
    }
    
    // Clear buffer to transparent black
    memset(shm_data, 0x00, shm_size);
    
    // Attach buffer to surface (required before first commit)
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(surface);
    
    // Now wait for the session to be locked
    wl_display_roundtrip(display);
    
    if (!session_is_locked) {
        fprintf(stderr, "Session lock was not established after creating lock surface\n");
        wl_buffer_destroy(buffer);
        ext_session_lock_surface_v1_destroy(lock_surface);
        wl_surface_destroy(surface);
        return NULL;
    }
    
    // Create Cairo surface from shared memory 
    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)shm_data,
        CAIRO_FORMAT_ARGB32,
        surface_width,
        surface_height,
        surface_width * 4
    );
    
    if (!cairo_surface) {
        fprintf(stderr, "Failed to create Cairo surface\n");
        wl_buffer_destroy(buffer);
        ext_session_lock_surface_v1_destroy(lock_surface);
        wl_surface_destroy(surface);
        return NULL;
    }
    
    // Store reference for flushing during commits
    current_cairo_surface = cairo_surface;
    
    return cairo_surface;
}

static void wayland_get_display_bounds(unsigned int monitor_num, display_bounds_t *bounds)
{
    bounds->x = 0;
    bounds->y = 0;
    bounds->width = surface_width;
    bounds->height = surface_height;
}

static void wayland_poll_events(void *state)
{
    if (!display) {
        return;
    }
    
    // Process Wayland events (non-blocking)
    if (wl_display_prepare_read(display) == 0) {
        wl_display_read_events(display);
    }

    wl_display_dispatch_pending(display);
    wl_display_flush(display);
}


static void wayland_commit_surface(void)
{
    if (!surface || !buffer || !surface_configured) {
        return;
    }
    
    wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface);
}

static void wayland_destroy_surface(cairo_surface_t *cairo_surface)
{
    if (cairo_surface) {
        cairo_surface_destroy(cairo_surface);
        current_cairo_surface = NULL;
    }
    
    if (shm_data && shm_size > 0) {
        munmap(shm_data, shm_size);
        shm_data = NULL;
        shm_size = 0;
    }
    
    if (buffer) {
        wl_buffer_destroy(buffer);
        buffer = NULL;
    }
    
    if (lock_surface) {
        ext_session_lock_surface_v1_destroy(lock_surface);
        lock_surface = NULL;
    }
    
    if (surface) {
        wl_surface_destroy(surface);
        surface = NULL;
    }
    
    // Reset state
    surface_configured = false;
}

static void wayland_unlock_session(void)
{
    if (session_lock) {
        ext_session_lock_v1_unlock_and_destroy(session_lock);
        session_lock = NULL;
        session_is_locked = false;
        
        // Give compositor time to process the unlock
        wl_display_roundtrip(display);
    }
}

static void wayland_cleanup(void)
{
    // Unlock session first if still locked
    if (session_is_locked) {
        wayland_unlock_session();
    }
    
    if (keyboard) {
        wl_keyboard_destroy(keyboard);
        keyboard = NULL;
    }
    
    if (seat) {
        wl_seat_destroy(seat);
        seat = NULL;
    }
    
    if (output) {
        wl_output_destroy(output);
        output = NULL;
    }
    
    if (session_lock) {
        ext_session_lock_v1_destroy(session_lock);
        session_lock = NULL;
    }
    
    if (session_lock_manager) {
        ext_session_lock_manager_v1_destroy(session_lock_manager);
        session_lock_manager = NULL;
    }
    
    if (compositor) {
        wl_compositor_destroy(compositor);
        compositor = NULL;
    }
    
    if (shm) {
        wl_shm_destroy(shm);
        shm = NULL;
    }
    
    if (registry) {
        wl_registry_destroy(registry);
        registry = NULL;
    }
    
    if (display) {
        wl_display_disconnect(display);
        display = NULL;
    }
}

#else // !HAVE_WAYLAND

// Fallback implementations when Wayland is not available
static bool wayland_init(void)
{
    fprintf(stderr, "Wayland support not compiled in\n");
    return false;
}

static cairo_surface_t* wayland_acquire_surface(void)
{
    return NULL;
}

static void wayland_get_display_bounds(unsigned int monitor_num, display_bounds_t *bounds)
{
    bounds->x = 0;
    bounds->y = 0;
    bounds->width = 1920;
    bounds->height = 1080;
}

static void wayland_poll_events(void *state)
{
    // No-op
}

static void wayland_commit_surface(void)
{
    // No-op
}

static void wayland_destroy_surface(cairo_surface_t *surface)
{
    // No-op
}

static void wayland_cleanup(void)
{
    // No-op
}

#endif // HAVE_WAYLAND


static void wayland_await_frame(void)
{
    // Wayland protocol is "don't call us, we call you" (commit frame). 
    // So do nothing here. 
}


// Wayland backend interface
const display_server_interface_t wayland_interface = {
    .init = wayland_init,
    .acquire_surface = wayland_acquire_surface,
    .get_display_bounds = wayland_get_display_bounds,
    .poll_events = wayland_poll_events,
    .commit_surface = wayland_commit_surface,
    .unlock_session = wayland_unlock_session,
    .destroy_surface = wayland_destroy_surface,
    .await_frame = wayland_await_frame,
    .cleanup = wayland_cleanup
};
