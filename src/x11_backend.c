/*
 * x11_backend.c
 * 
 * X11 backend implementation for display server abstraction
 * Created 2025-05-28 by James Magahern <james@magahern.com>
 */

#include "display_server.h"
#include "render.h"
#include "events.h"
#include "animation.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h> 

#include <cairo-xlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const int kXSecureLockCharFD = 0;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} x11_display_bounds_t;


static Window __window = { 0 };
static Display *__display = NULL;

static void x11_get_display_bounds_w(Window window, unsigned int monitor_num, x11_display_bounds_t *out_bounds);

static void get_window_from_environment_or_make_one(Window *window, Display *display, int *out_width, int *out_height)
{
    Window parent_window;

    Window root_window = DefaultRootWindow(__display);
    const char *env_window = getenv("XSCREENSAVER_WINDOW");
    if (env_window != NULL && env_window[0] != 0) {
        char *endptr = NULL;
        unsigned long long number = strtoull(env_window, &endptr, 0);
        root_window = (Window)number;

        // Get parent window
        unsigned int unused_num_children = 0;
        Window unused_root, *unused_children = NULL;
        XQueryTree(display, root_window, &unused_root, &parent_window, &unused_children, &unused_num_children);
    } else {
        parent_window = root_window;
    }

    // Figure out which monitor this is supposed to go on
    const unsigned int preferred_monitor = get_preferred_monitor_num();
    x11_display_bounds_t bounds;
    x11_get_display_bounds_w(root_window, preferred_monitor, &bounds);

    *window = XCreateSimpleWindow(
            display,        // display
            parent_window,  // parent window
            bounds.x,       // x 
            bounds.y,       // y
            bounds.width,   // width
            bounds.height,  // height
            0,              // border_width
            0,              // border
            0               // background
    );

    *out_width = bounds.width;
    *out_height = bounds.height;
}

void x11_get_display_bounds(unsigned int monitor_num, x11_display_bounds_t *out_bounds)
{
    x11_get_display_bounds_w(__window, monitor_num, out_bounds);
}

static void x11_get_display_bounds_w(Window window, unsigned int monitor_num, x11_display_bounds_t *out_bounds)
{
    int num_monitors = 0;
    XRRMonitorInfo *monitor_infos = XRRGetMonitors(__display, window, True, &num_monitors);
    if (num_monitors == 0) {
        fprintf(stderr, "FATAL: Couldn't get monitor info from XRandR!\n");
        exit(1);
    }

    unsigned int idx = monitor_num;
    if (idx > num_monitors) {
        fprintf(stderr, "WARNING: Specified monitor number is greater than the number of connected monitors!\n");
        idx = 0;
    }

    XRRMonitorInfo *monitor = &monitor_infos[idx];
    out_bounds->x = monitor->x;
    out_bounds->y = monitor->y;
    out_bounds->width = monitor->width;
    out_bounds->height = monitor->height;
}

cairo_surface_t* x11_helper_acquire_cairo_surface()
{
    __display = XOpenDisplay(NULL);
    if (__display == NULL) {
        fprintf(stderr, "Error opening display\n");
        return NULL;
    }

    // Create (or get) window
    int width, height;
    get_window_from_environment_or_make_one(&__window, __display, &width, &height);

    // Event mask
    XSelectInput(__display, __window, ButtonPressMask | KeyPressMask | StructureNotifyMask);

    // Map window to display
    XMapWindow(__display, __window);

    // Create cairo surface
    int screen = DefaultScreen(__display);
    Visual *visual = DefaultVisual(__display, screen);

    cairo_surface_t *surface = cairo_xlib_surface_create(
            __display, 
            __window,
            visual, 
            width, 
            height
    );

    return surface;
}

void x11_helper_destroy_surface(cairo_surface_t *surface)
{
    cairo_surface_destroy(surface);
    XCloseDisplay(__display);
}

/** display_server_interface implementation **/

static void x11_poll_events(void *state);

static bool x11_init(void)
{
    // X11 initialization is handled in x11_helper_acquire_cairo_surface
    return true;
}

static cairo_surface_t* x11_acquire_surface(void)
{
    return x11_helper_acquire_cairo_surface();
}

static void x11_backend_get_display_bounds(unsigned int monitor_num, display_bounds_t *bounds)
{
    x11_display_bounds_t x11_bounds;
    x11_get_display_bounds(monitor_num, &x11_bounds);
    
    bounds->x = x11_bounds.x;
    bounds->y = x11_bounds.y;
    bounds->width = x11_bounds.width;
    bounds->height = x11_bounds.height;
}

static void post_keyboard_event(saver_state_t *state, event_type_t type, char letter)
{
    event_t event = {
        .codepoint = letter,
        .type = type
    };

    queue_event(event);
}

// This input handler is only when the locker is being run in "X11 mode" for development
// In production, handle_xsl_key_input is used exclusively. 
bool handle_key_event(saver_state_t *state, XKeyEvent *event)
{
    if (!state->input_allowed) return false;

    KeySym key;
    char keybuf[8];
    XLookupString(event, keybuf, sizeof(keybuf), &key, NULL);

    bool handled = true;
    if (XK_BackSpace == key) {
        post_keyboard_event(state, EVENT_KEYBOARD_BACKSPACE, 0);
    } else if (XK_Return == key) {
        post_keyboard_event(state, EVENT_KEYBOARD_RETURN, 0);
    } else if (strlen(keybuf) > 0) {
        post_keyboard_event(state, EVENT_KEYBOARD_LETTER, keybuf[0]);
    } else {
        handled = false;
    }

    return handled;
}

// The following two methods are separate methods for processing input:
// The first one `handle_xsl_key_input` is for handling input via the XSecureLock
// file descriptor, which basically gives us TTY keycodes. The second handles
// input via X11, which is really only used for testing (when the locker is being
// run inside a window during development).
static void handle_xsl_key_input(saver_state_t *state, const char c)
{
    if (!state->input_allowed) return;

    char *password_buf = state->password_buffer;
    size_t pw_len = strlen(password_buf);
    switch (c) {
        case '\b':      // Backspace.
            if (pw_len > 0) {
                password_buf[pw_len - 1] = '\0';
            }
            break;
        case '\177':  // Delete
            break;
        case '\001':  // Ctrl-A.
            // TODO: cursor movement
            break;
        case '\025':  // Ctrl-U.
            post_keyboard_event(state, EVENT_KEYBOARD_CLEAR, 0);
            break;
        case 0:       // Shouldn't happen.
        case '\033':  // Escape.
            break;
        case '\r':  // Return.
        case '\n':  // Return.
            post_keyboard_event(state, EVENT_KEYBOARD_RETURN, 0);
            break;
        default:
            post_keyboard_event(state, EVENT_KEYBOARD_LETTER, c);
            break;
    }
}

static void x11_poll_events(void *state)
{
    saver_state_t *saver_state = (saver_state_t *)state;
    
    XEvent e;
    bool handled_key_event = false;
    const bool block_for_next_event = false;

    // Via xsecurelock, take this route
    char buf;
    ssize_t read_res = read(kXSecureLockCharFD, &buf, 1);
    if (read_res > 0) {
        handle_xsl_key_input(saver_state, buf);
        handled_key_event = true;
    }

    // Handle X11 events
    Display *display = cairo_xlib_surface_get_display(saver_state->surface);
    for (;;) {
        if (block_for_next_event || XPending(display)) {
            XNextEvent(display, &e);
        } else {
            break;
        }

        switch (e.type) {
            case ConfigureNotify:
                post_keyboard_event(state, EVENT_SURFACE_SIZE_CHANGED, 0);
                break;
            case ButtonPress:
                break;
            case KeyPress:
                handled_key_event = handle_key_event(saver_state, (XKeyEvent *)&e);
                break;
            default:
                fprintf(stderr, "Dropping unhandled X event.type = %d.\n", e.type);
                break;
        }
    }

    if (handled_key_event) {
        // Mark password layer dirty
        set_layer_needs_draw(saver_state, LAYER_PASSWORD, true);
    }
}

static void x11_commit_surface(void)
{
    // No-op for X11 - surface updates are immediate
}

static void x11_unlock_session(void)
{
    // No-op for X11 - no session lock protocol. 
    // We just exit with status 0 and Xsecurelock does the rest. 
}

static void x11_cleanup(void)
{
    // X11 cleanup is handled in x11_helper_destroy_surface
}

static void x11_await_frame(void)
{
    const int frames_per_sec = 60; // TODO: probably should get this from xrandr. 
    const long sleep_nsec = (1.0 / frames_per_sec) * 1000000000;
    struct timespec sleep_time = { 0, sleep_nsec };
    nanosleep(&sleep_time, NULL);
}

// X11 backend interface
const display_server_interface_t x11_interface = {
    .init = x11_init,
    .acquire_surface = x11_acquire_surface,
    .get_display_bounds = x11_backend_get_display_bounds,
    .poll_events = x11_poll_events,
    .commit_surface = x11_commit_surface,
    .unlock_session = x11_unlock_session,
    .await_frame = x11_await_frame,
    .destroy_surface = x11_helper_destroy_surface,
    .cleanup = x11_cleanup
};