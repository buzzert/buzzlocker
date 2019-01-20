/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include "auth.h"
#include "render.h"
#include "x11_support.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static const size_t kMaxPasswordLength = 128;

static inline saver_state_t* saver_state(void *c)
{
    return (saver_state_t *)c;
}

static void accept_password(saver_state_t *state);

/*
 * Event handling
 */

static void window_changed_size(saver_state_t *state, XConfigureEvent *event)
{
    state->canvas_width = event->width;
    state->canvas_height = event->height;

    cairo_xlib_surface_set_size(state->surface, event->width, event->height);
}

static void handle_key_event(saver_state_t *state, XKeyEvent *event)
{
    if (!state->input_allowed) return;

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
    } else if (XK_Return == key) {
        accept_password(state);
    } else if (strlen(keybuf) > 0) {
        size_t add_len = strlen(keybuf);
        if ( (length + add_len) < state->password_buffer_len - 1 ) {
            strncpy(password_buf + length, keybuf, add_len + 1);
        }
    }
}

static int poll_events(saver_state_t *state)
{
    XEvent e;
    const bool block_for_next_event = false;

    // Temp: this should be handled by x11_support
    Display *display = cairo_xlib_surface_get_display(state->surface);
    for (;;) {
        if (block_for_next_event || XPending(display)) {
            // XNextEvent blocks the caller until an event arrives
            XNextEvent(display, &e);
        } else {
            return 0;
        }

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
 * Actions
 */

static void accept_password(saver_state_t *state)
{
    state->cursor_animating = false;

    size_t pw_length = strlen(state->password_buffer);
    char *password_buf = malloc(pw_length);
    strncpy(password_buf, state->password_buffer, pw_length + 1);

    auth_prompt_response_t response;
    response.response_buffer = password_buf;
    response.response_code = 0;
    auth_attempt_authentication(state->auth_handle, response);

    // Block input until we hear back from the auth thread
    state->input_allowed = false;
}

/*
 * Auth callbacks
 */

void callback_show_info(const char *info_msg, void *context)
{
    saver_state(context)->password_prompt = info_msg;
}

void callback_show_error(const char *error_msg, void *context)
{
    saver_state(context)->password_prompt = error_msg;
}

void callback_prompt_user(const char *prompt, void *context)
{
    size_t prompt_len = strlen(prompt);
    char *new_prompt = malloc(prompt_len);
    strncpy(new_prompt, prompt, prompt_len + 1);

    saver_state_t *state = saver_state(context);
    state->password_prompt = new_prompt;
    state->cursor_animating = true;
    state->input_allowed = true;
}

void callback_authentication_result(int result, void *context)
{
    saver_state_t *state = saver_state(context);
    if (result == 0) {
        state->is_authenticated = true;
    } else {
        // Try again
        state->password_buffer[0] = '\0';
    }
}

/*
 * Main drawing/update routines 
 */

static void update(saver_state_t *state)
{
    if (state->cursor_animating) {
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
    } else {
        state->cursor_opacity = 1.0;
    }

    poll_events(state);
}

static void draw(saver_state_t *state)
{
    // Draw background
    cairo_t *cr = state->ctx;
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(cr);

    draw_logo(state);
    draw_password_field(state);
}

static int runloop(cairo_surface_t *surface)
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
    state.cursor_animating = false;
    state.input_allowed = false;
    state.password_prompt = "";
    state.is_authenticated = false;

    auth_callbacks_t callbacks = {
        .info_handler = callback_show_info,
        .error_handler = callback_show_error,
        .prompt_handler = callback_prompt_user,
        .result_handler = callback_authentication_result
    };

    state.auth_handle = auth_begin_authentication(callbacks, &state);

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

        if (state.is_authenticated) {
            // We're done here
            break;
        }
    }

    // Cleanup
    cairo_destroy(cr);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int default_width = 800;
    int default_height = 600;

    cairo_surface_t *surface = x11_helper_acquire_cairo_surface(default_width, default_height);
    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Docs say this must be called whenever the size of the window changes
    cairo_xlib_surface_set_size(surface, default_width, default_height);
    
    int result = runloop(surface);

    x11_helper_destroy_surface(surface);
    return result;
}

