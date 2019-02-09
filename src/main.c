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

static const char *kDefaultFont = "Input Mono 22";

static inline saver_state_t* saver_state(void *c)
{
    return (saver_state_t *)c;
}

static void clear_password(saver_state_t *state);
static void accept_password(saver_state_t *state);

static int poll_events(saver_state_t *state);

static void handle_key_event(saver_state_t *state, XKeyEvent *event);
static void handle_xsl_key_input(saver_state_t *state, const char c);
static void window_changed_size(saver_state_t *state, XConfigureEvent *event);

static void ending_animation_completed(struct animation_t *animation, void *context);
static void authentication_accepted(saver_state_t *state);
static void authentication_rejected(saver_state_t *state);

static void update(saver_state_t *state);
static void draw(saver_state_t *state);
static int runloop(cairo_surface_t *surface);

void callback_show_info(const char *info_msg, void *context);
void callback_show_error(const char *error_msg, void *context);
void callback_prompt_user(const char *prompt, void *context);
void callback_authentication_result(int result, void *context);

/*
 * Event handling
 */

static void window_changed_size(saver_state_t *state, XConfigureEvent *event)
{
    state->canvas_width = event->width;
    state->canvas_height = event->height;

    cairo_xlib_surface_set_size(state->surface, event->width, event->height);
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
            if (pw_len + 1 < kMaxPasswordLength) {
                password_buf[pw_len] = c;
                password_buf[pw_len + 1] = '\0';
            }
            break;
    }
}

// This input handler is only when the locker is being run in "X11 mode" for development
// (See comment above for why this is separate)
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
        if (length + 1 < kMaxPasswordLength) {
            password_buf[length] = keybuf[0];
            password_buf[length + 1] = '\0';
        }
    }

    set_layer_needs_draw(state, LAYER_PASSWORD, true);
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
    auth_prompt_response_t response;
    strncpy(response.response_buffer, state->password_buffer, MAX_RESPONSE_SIZE);
    response.response_code = 0;
    auth_attempt_authentication(state->auth_handle, response);

    // Block input until we hear back from the auth thread
    state->is_processing = true;
    state->input_allowed = false;

    // Spinner animation
    if (state->spinner_anim_key == ANIM_KEY_NOEXIST) {
        state->spinner_anim_key = schedule_animation(state, (animation_t) {
            .type = ASpinnerAnimation,
            .anim.spinner_anim = { 0 }
        });
    }

    // Update prompt
    set_password_prompt(state, "Authenticating...");
}

static void ending_animation_completed(struct animation_t *animation, void *context)
{
    saver_state_t *state = saver_state(context);
    state->is_authenticated = true;
}

static void authentication_accepted(saver_state_t *state)
{
    state->is_processing = false;
    set_password_prompt(state, "Welcome");
    clear_password(state);

    // Stop cursor animation
    animation_t *cursor_anim = get_animation_for_key(state, state->cursor_anim_key);
    if (cursor_anim) {
        cursor_anim->anim.cursor_anim.cursor_animating = false;
        state->cursor_opacity = 0.0;
    }

    animation_t out_animation = {
        .type = ALogoAnimation,
        .completion_func = ending_animation_completed,
        .completion_func_context = state,
        .direction = OUT,
    };
    schedule_animation(state, out_animation);
}

static void authentication_rejected(saver_state_t *state)
{
    animation_t flash_animation = {
        .type = ARedFlashAnimation,
        .direction = IN,
    };
    schedule_animation(state, flash_animation);

    clear_password(state);
}

/*
 * Auth callbacks
 */

void callback_show_info(const char *info_msg, void *context)
{
    set_password_prompt(saver_state(context), info_msg);
}

void callback_show_error(const char *error_msg, void *context)
{
    set_password_prompt(saver_state(context), error_msg);
}

void callback_prompt_user(const char *prompt, void *context)
{
    saver_state_t *state = saver_state(context);
    set_password_prompt(state, prompt);
    state->input_allowed = true;
    state->is_processing = false;
    set_layer_needs_draw(state, LAYER_PROMPT, true);
}

void callback_authentication_result(int result, void *context)
{
    saver_state_t *state = saver_state(context);
    if (result == 0) {
        authentication_accepted(state);
    } else {
        // Try again
        authentication_rejected(state);
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
    if (layer_needs_draw(state, LAYER_BACKGROUND)) {
        draw_background(state, 0, 0, state->canvas_width, state->canvas_height);
    }

    if (layer_needs_draw(state, LAYER_LOGO)) {
        draw_logo(state);
    }

    draw_password_field(state);

    // Automatically reset this after every draw call
    set_layer_needs_draw(state, LAYER_BACKGROUND, false);
}

static int runloop(cairo_surface_t *surface)
{
    cairo_t *cr = cairo_create(surface);

    // Initialize pango context
    PangoLayout *pango_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *status_font = pango_font_description_from_string(kDefaultFont);

    saver_state_t state = { 0 };
    state.ctx = cr;
    state.surface = surface;
    state.cursor_opacity = 1.0;
    state.pango_layout = pango_layout;
    state.status_font = status_font;
    state.input_allowed = false;
    state.is_authenticated = false;
    state.is_processing = false;
    state.spinner_anim_key = ANIM_KEY_NOEXIST;

    // Add initial animations
    // Cursor animation -- repeats indefinitely
    animation_t cursor_animation = {
        .type = ACursorAnimation,
        .direction = OUT,
        .anim.cursor_anim = {
            .cursor_animating = true
        }
    };
    state.cursor_anim_key = schedule_animation(&state, cursor_animation);

    // Logo incoming animation
    animation_t logo_animation = {
        .type = ALogoAnimation,
        .direction = IN,
    };
    schedule_animation(&state, logo_animation);

    x11_display_bounds_t bounds;
    x11_get_display_bounds(get_preferred_monitor_num(), &bounds);
    state.canvas_width = bounds.width;
    state.canvas_height = bounds.height;

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
    cairo_surface_t *surface = x11_helper_acquire_cairo_surface();
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

