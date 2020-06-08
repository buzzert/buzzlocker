/*
 * x11_support.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-18
 */

#include "x11_support.h"

#include <X11/extensions/Xrandr.h> 
#include <stdio.h>
#include <stdlib.h>

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

unsigned int get_preferred_monitor_num()
{
    const char *preferred_monitor = getenv("BUZZLOCKER_MONITOR_NUM");
    if (preferred_monitor != NULL && preferred_monitor[0] != 0) {
        char *endptr = NULL;
        unsigned long int result = strtoul(preferred_monitor, &endptr, 0);
        return result;
    }

    return 0;
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

    // Enable key events
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

