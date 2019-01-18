/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include "render.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Window __window = { 0 };
static Display *__display = NULL;

static const size_t kMaxPasswordLength = 128;

void window_changed_size(saver_state_t *state, XConfigureEvent *event)
{
    state->canvas_width = event->width;
    state->canvas_height = event->height;

    cairo_xlib_surface_set_size(state->surface, event->width, event->height);
}

void handle_key_event(saver_state_t *state, XKeyEvent *event)
{
    KeySym key;
    char keybuf[8];
    XLookupString(event, keybuf, sizeof(keybuf), &key, NULL);

    char *password_buf = state->password_buffer;
    size_t length = strlen(password_buf);
    if (XK_BackSpace == key) {
        // delete char
        if (length > 0) {
            password_buf[length - 1] = '\0';
        }
    } else if (strlen(keybuf) > 0) {
        size_t add_len = strlen(keybuf);
        if ( (length + add_len) < state->password_buffer_len - 1 ) {
            strncpy(password_buf + length, keybuf, add_len);
        }
    }
}

int poll_events(saver_state_t *state)
{
    const bool block_for_next_event = false;
    XEvent e;

    for (;;) {
        if (block_for_next_event || XPending(__display)) {
            // XNextEvent blocks the caller until an event arrives
            XNextEvent(__display, &e);
        } else {
            return 0;
        }

        // TODO: listen for window resize events and resize cairo surface
        switch (e.type) {
            case ConfigureNotify:
                window_changed_size(state, (XConfigureEvent *)&e);
                return 1;
            case ButtonPress:
                return -e.xbutton.button;
            case KeyPress:
                handle_key_event(state, (XKeyEvent *)&e);
                return 1;
            default:
                fprintf(stderr, "Dropping unhandled XEevent.type = %d.\n", e.type);
        }
    }

    return 0;
}

/*
 * Main drawing/update routines 
 */

void update(saver_state_t *state)
{
    const double cursor_fade_speed = 0.007;
    if (state->cursor_fade_direction > 0) {
        state->cursor_opacity += cursor_fade_speed;
        if (state->cursor_opacity > 1.0) {
            state->cursor_fade_direction *= -1;
        }
    } else {
        state->cursor_opacity -= cursor_fade_speed;
        if (state->cursor_opacity <= 0.0) {
            state->cursor_fade_direction *= -1;
        }
    }


    poll_events(state);
}

void draw(saver_state_t *state)
{
    // Draw background
    cairo_t *cr = state->ctx;
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(cr);

    draw_logo(state);
    draw_password_field(state);
}

int runloop(cairo_surface_t *surface)
{
    cairo_t *cr = cairo_create(surface);

    // Initialize pango context
    PangoLayout *pango_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *status_font = pango_font_description_from_string("Input Mono 22");

    saver_state_t state = { 0 };
    state.ctx = cr;
    state.surface = surface;
    state.cursor_opacity = 1.0;
    state.cursor_fade_direction = -1.0;
    state.pango_layout = pango_layout;
    state.status_font = status_font;
    state.password_buffer = calloc(1, kMaxPasswordLength);
    state.password_buffer_len = kMaxPasswordLength;

    // Main run loop
    struct timespec sleep_time = { 0, 5000000 };
    for (;;) {
        update(&state);

        cairo_push_group(cr);
        
        draw(&state);
        
        cairo_pop_group_to_source(cr);

        cairo_paint(cr);
        cairo_surface_flush(surface);

        nanosleep(&sleep_time, NULL);
    }

    // Cleanup
    cairo_destroy(cr);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int default_width = 800;
    int default_height = 600;

    __display = XOpenDisplay(NULL);
    if (__display == NULL) {
        fprintf(stderr, "Error opening display\n");
        exit(1);
    }

    Window root_window = DefaultRootWindow(__display);
    __window = XCreateSimpleWindow(
            __display,      // display
            root_window,    // parent window
            0, 0,           // x, y
            default_width,  // width
            default_height, // height
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
            default_width, 
            default_height
    );

    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Docs say this must be called whenever the size of the window changes
    cairo_xlib_surface_set_size(surface, default_width, default_height);
    
    int result = runloop(surface);

    cairo_surface_destroy(surface);
    XCloseDisplay(__display);

    return result;
}

