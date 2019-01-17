/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include <cairo/cairo.h>
#include <cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int __width, __height;
static Window __window = { 0 };
static Display *__display = NULL;

static const size_t kMaxPasswordLength = 128;
static const double kLogoBackgroundWidth = 500.0;

typedef struct {
    cairo_t                *ctx;
    cairo_surface_t        *surface;

    PangoLayout            *pango_layout;
    PangoFontDescription   *status_font;

    RsvgHandle             *logo_svg_handle;
    RsvgHandle             *asterisk_svg_handle;

    double                  cursor_opacity;
    double                  cursor_fade_direction;

    char                   *password_buffer;
    size_t                  password_buffer_len;
} saver_state_t;

void window_changed_size(saver_state_t *state, XConfigureEvent *event)
{
    __width = event->width;
    __height = event->height;

    cairo_xlib_surface_set_size(state->surface, __width, __height);
}

void handle_key_event(saver_state_t *state, XKeyEvent *event)
{
    KeySym key;
    char keybuf[8];
    XLookupString(event, keybuf, sizeof(keybuf), &key, NULL);
    printf("str: %d\n", key);

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
 * Scene specific stuff
 */

void draw_logo(saver_state_t *state)
{
    if (state->logo_svg_handle == NULL) {
        GError *error = NULL;
        state->logo_svg_handle = rsvg_handle_new_from_file("logo.svg", &error);
        if (error != NULL) {
            fprintf(stderr, "Error loading logo SVG\n");
            return;
        }
    }

    cairo_t *cr = state->ctx;

    cairo_save(cr);
    cairo_set_source_rgb(cr, (208.0 / 255.0), (69.0 / 255.0), (255.0 / 255.0));
    cairo_rectangle(cr, 0, 0, kLogoBackgroundWidth, __height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    
    // Scale and draw logo
    RsvgDimensionData dimensions;
    rsvg_handle_get_dimensions(state->logo_svg_handle, &dimensions);

    const double padding = 100.0;
    double scale_factor = ((kLogoBackgroundWidth - (padding * 2.0)) / dimensions.width);
    double scaled_height = (dimensions.height * scale_factor);
    double y_position = (__height - scaled_height) / 2.0;
    cairo_translate(cr, padding, y_position);
    cairo_scale(cr, scale_factor, scale_factor);
    rsvg_handle_render_cairo(state->logo_svg_handle, cr);

    cairo_restore(cr);
}

void draw_password_field(saver_state_t *state)
{
    const double cursor_height = 40.0;
    const double cursor_width  = 30.0;
    const double field_x = kLogoBackgroundWidth + 50.0;
    const double field_y = (__height - cursor_height) / 2.0;
    const double field_padding = 10.0;
    
    cairo_t *cr = state->ctx;

    // Draw status text
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    pango_layout_set_font_description(state->pango_layout, state->status_font);
    pango_layout_set_text(state->pango_layout, "Password: ", -1);

    int t_width, t_height;
    pango_layout_get_size(state->pango_layout, &t_width, &t_height);
    double line_height = t_height / PANGO_SCALE;

    cairo_move_to(cr, field_x, field_y - line_height - field_padding);
    pango_cairo_show_layout(cr, state->pango_layout);

    // Draw password asterisks
    if (state->asterisk_svg_handle == NULL) {
        GError *error = NULL;
        state->asterisk_svg_handle = rsvg_handle_new_from_file("asterisk.svg", &error);
        if (error != NULL) {
            fprintf(stderr, "Error loading asterisk SVG\n");
            return;
        }
    }

    const double cursor_padding_x = 10.0;
    double cursor_offset_x = 0.0;

    RsvgDimensionData dimensions;
    rsvg_handle_get_dimensions(state->asterisk_svg_handle, &dimensions);
    
    double asterisk_height = cursor_height - 20.0;
    double scale_factor = (asterisk_height / dimensions.height);
    double scaled_width = (dimensions.width * scale_factor);

    for (unsigned i = 0; i < strlen(state->password_buffer); i++) {
        cairo_save(cr);
        cairo_translate(cr, field_x + cursor_offset_x, field_y + ((cursor_height - asterisk_height) / 2.0));
        cairo_scale(cr, scale_factor, scale_factor);
        rsvg_handle_render_cairo(state->asterisk_svg_handle, cr);
        cairo_restore(cr);

        cursor_offset_x += scaled_width + cursor_padding_x;
    }

    
    // Draw cursor
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, state->cursor_opacity);
    cairo_rectangle(cr, field_x + cursor_offset_x, field_y, cursor_width, cursor_height);
    cairo_fill(cr);
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
    __width = 800;
    __height = 600;

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
            __width,        // width
            __height,       // height
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
            __width, 
            __height
    );

    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Docs say this must be called whenever the size of the window changes
    cairo_xlib_surface_set_size(surface, __width, __height);
    
    int result = runloop(surface);

    cairo_surface_destroy(surface);
    XCloseDisplay(__display);

    return result;
}

