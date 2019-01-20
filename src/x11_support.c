/*
 * x11_support.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-18
 */

#include "x11_support.h"

#include <stdio.h>
#include <stdlib.h>

static Window __window = { 0 };
static Display *__display = NULL;

static Window get_window_from_environment_or_make_one(Display *display, int width, int height)
{
    Window window;

    const char *env_window = getenv("XSCREENSAVER_WINDOW");
    if (env_window != NULL && env_window[0] != 0) {
        char *endptr = NULL;
        unsigned long long number = strtoull(env_window, &endptr, 0);
        window = (Window)number;
    } else {
        // Presumably this is for debugging
        Window root_window = DefaultRootWindow(__display);
        window = XCreateSimpleWindow(
                display,        // display
                root_window,    // parent window
                0, 0,           // x, y
                width,          // width
                height,         // height
                0,              // border_width
                0,              // border
                0               // background
        );
    }

    return window;
}

cairo_surface_t* x11_helper_acquire_cairo_surface(int width, int height)
{
    __display = XOpenDisplay(NULL);
    if (__display == NULL) {
        fprintf(stderr, "Error opening display\n");
        return NULL;
    }

    // Create (or get) window
    __window = get_window_from_environment_or_make_one(__display, width, height);

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

