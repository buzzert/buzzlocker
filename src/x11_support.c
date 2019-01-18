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

cairo_surface_t* x11_helper_acquire_cairo_surface(int width, int height)
{
    __display = XOpenDisplay(NULL);
    if (__display == NULL) {
        fprintf(stderr, "Error opening display\n");
        return NULL;
    }

    Window root_window = DefaultRootWindow(__display);
    __window = XCreateSimpleWindow(
            __display,      // display
            root_window,    // parent window
            0, 0,           // x, y
            width,          // width
            height,         // height
            0,              // border_width
            0,              // border
            0               // background
    );

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

