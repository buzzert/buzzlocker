/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include <cairo/cairo.h>
#include <cairo-xlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int poll_events(Display *display, Window window)
{
    const bool block_for_next_event = false;
    char keybuf[8];
    KeySym key;
    XEvent e;

    for (;;) {
        if (block_for_next_event || XPending(display)) {
            // XNextEvent blocks the caller until an event arrives
            XNextEvent(display, &e);
        } else {
            return 0;
        }

        // TODO: listen for window resize events and resize cairo surface
        switch (e.type) {
            case ButtonPress:
                return -e.xbutton.button;
            case KeyPress:
                XLookupString(&e.xkey, keybuf, sizeof(keybuf), &key, NULL);
                return key;
            default:
                fprintf(stderr, "Dropping unhandled XEevent.type = %d.\n", e.type);
        }
    }
}

int main(int argc, char **argv)
{
    int width = 300;
    int height = 300;

    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Error opening display\n");
        exit(1);
    }

    Window root_window = DefaultRootWindow(display);
    Window window = XCreateSimpleWindow(
            display,        // display
            root_window,    // parent window
            0, 0,           // x, y
            width, height,  // width, height
            0,              // border_width
            0,              // border
            0               // background
    );

    // Enable key events
    XSelectInput(display, window, ButtonPressMask | KeyPressMask | StructureNotifyMask);

    // Map window to display
    XMapWindow(display, window);

    // Create cairo surface
    int screen = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);
    cairo_surface_t *surface = cairo_xlib_surface_create(
            display, 
            window,
            visual, 
            width, 
            height
    );

    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Docs say this must be called whenever the size of the window changes
    cairo_xlib_surface_set_size(surface, width, height);

    // Create cairo surface
    cairo_t *cr = cairo_create(surface);

    // Main run loop
    struct timespec sleep_time = { 0, 5000000 };
    for (;;) {
        cairo_push_group(cr);
        cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
        cairo_fill_preserve(cr);
        cairo_paint(cr);
        cairo_pop_group_to_source(cr);

        cairo_paint(cr);
        cairo_surface_flush(surface);

        poll_events(display, window);
        nanosleep(&sleep_time, NULL);
    }

    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    XCloseDisplay(display);

    return 0;
}

