/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include "auth.h"
#include "render.h"
#include "x11_support.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static const int kXSecureLockCharFD = 0;
static const size_t kMaxPasswordLength = 128;

static const int kDefaultWidth = 800;
static const int kDefaultHeight = 600;

static inline saver_state_t* saver_state(void *c)
{
    return (saver_state_t *)c;
}

static void clear_password(saver_state_t *state);
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
            clear_password(state);
            break;
        case 0:       // Shouldn't happen.
        case '\033':  // Escape.
            break;
        case '\r':  // Return.
        case '\n':  // Return.
            accept_password(state);
            break;
        default:
            if (pw_len + 1 < state->password_buffer_len) {
                password_buf[pw_len] = c;
                password_buf[pw_len + 1] = '\0';
            }
            break;
    }
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

    // Via xsecurelock, take this route
    char buf;
    ssize_t read_res = read(kXSecureLockCharFD, &buf, 1);
    if (read_res > 0) {
        handle_xsl_key_input(state, buf);
    }

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

static void clear_password(saver_state_t *state)
{
    state->password_buffer[0] = '\0';
}

static void accept_password(saver_state_t *state)
{
    size_t pw_length = strlen(state->password_buffer);
    char *password_buf = malloc(pw_length);
    strncpy(password_buf, state->password_buffer, pw_length + 1);

    auth_prompt_response_t response;
    response.response_buffer = password_buf;
    response.response_code = 0;
    auth_attempt_authentication(state->auth_handle, response);

    // Block input until we hear back from the auth thread
    state->is_processing = true;
    state->input_allowed = false;
}

static void ending_animation_completed(struct animation_t *animation, void *context)
{
    saver_state_t *state = saver_state(context);
    state->is_authenticated = true;
}

static void authentication_accepted(saver_state_t *state)
{
    animation_t out_animation = {
        .completed = false,
        .completion_func = ending_animation_completed,
        .completion_func_context = state,
        .anim.logo_anim = {
            .type = ALogoAnimation,
            .direction = true
        }
    };
    schedule_animation(state, out_animation);
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
    state->input_allowed = true;
    state->is_processing = false;
}

void callback_authentication_result(int result, void *context)
{
    saver_state_t *state = saver_state(context);
    if (result == 0) {
        authentication_accepted(state);
    } else {
        // Try again
        clear_password(state);
    }
}

/*
 * Main drawing/update routines 
 */

static void update(saver_state_t *state)
{
    update_animations(state);
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
    state.pango_layout = pango_layout;
    state.status_font = status_font;
    state.password_buffer = calloc(1, kMaxPasswordLength);
    state.password_buffer_len = kMaxPasswordLength;
    state.input_allowed = false;
    state.password_prompt = "";
    state.is_authenticated = false;
    state.is_processing = false;

    // Add initial animations
    // Cursor animation -- repeats indefinitely
    animation_t cursor_animation = {
        .completed = false,
        .anim.cursor_anim = {
            .type = ACursorAnimation,
            .cursor_fade_direction = -1.0,
            .cursor_animating = true
        }
    };
    schedule_animation(&state, cursor_animation);

    // Logo incoming animation
    animation_t logo_animation = {
        .completed = false,
        .anim.logo_anim = {
            .type = ALogoAnimation,
            .direction = false
        }
    };
    schedule_animation(&state, logo_animation);

    x11_get_display_bounds(&state.canvas_width, &state.canvas_height);

    // Docs say this must be called whenever the size of the window changes
    cairo_xlib_surface_set_size(surface, state.canvas_width, state.canvas_height);

    auth_callbacks_t callbacks = {
        .info_handler = callback_show_info,
        .error_handler = callback_show_error,
        .prompt_handler = callback_prompt_user,
        .result_handler = callback_authentication_result
    };

    state.auth_handle = auth_begin_authentication(callbacks, &state);

    // Main run loop
    const int frames_per_sec = 60;
    const long sleep_nsec = (1.0 / frames_per_sec) * 1000000000;
    struct timespec sleep_time = { 0, sleep_nsec };
    while (!state.is_authenticated) {
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
    int default_width = kDefaultWidth;
    int default_height = kDefaultHeight;

    cairo_surface_t *surface = x11_helper_acquire_cairo_surface(default_width, default_height);
    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Make it so reading from the xsecurelock file descriptor doesn't block
    int flags = fcntl(kXSecureLockCharFD, F_GETFL, 0);
    fcntl(kXSecureLockCharFD, F_SETFL, flags | O_NONBLOCK);

    int result = runloop(surface);

    x11_helper_destroy_surface(surface);
    return result;
}

