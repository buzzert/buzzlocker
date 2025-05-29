/*
 * buzzsaver.c
 * 
 * Created 2019-01-16 by James Magahern <james@magahern.com>
 */

#include "auth.h"
#include "render.h"
#include "display_server.h"
#include "events.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static const int kXSecureLockCharFD = 0;

static const char *kDefaultFont = "Input Mono 22";
static const char *kClockFont = "Sans Italic 20";

static const char *kEnableClockEnvVar = "BUZZLOCKER_ENABLE_CLOCK";

static inline saver_state_t* saver_state(void *c)
{
    return (saver_state_t *)c;
}

static void accept_password(saver_state_t *state);
static void clear_password(saver_state_t *state);

// Make these functions available to backends
bool handle_key_event(saver_state_t *state, XKeyEvent *event);
void handle_xsl_key_input(saver_state_t *state, const char c);
void window_changed_size(saver_state_t *state, XConfigureEvent *event);
static void reset_cursor_flash_anim(saver_state_t *state);

static void ending_animation_completed(struct animation_t *animation, void *context);
static void authentication_accepted(saver_state_t *state);
static void authentication_rejected(saver_state_t *state);

static timer_id push_timer(saver_state_t *state, saver_timer_t *timer);
void reset_timer(saver_state_t *state, timer_id timerid, anim_time_interval_t duration);
void cancel_timer(saver_state_t *state, timer_id timer);

static void draw(saver_state_t *state);
static void timers(saver_state_t *state);
static int runloop(saver_state_t *state);

void callback_show_info(const char *info_msg, void *context);
void callback_show_error(const char *error_msg, void *context);
void callback_prompt_user(const char *prompt, void *context);
void callback_authentication_result(int result, void *context);
void callback_show_auth_progress(void *context);
void callback_update_clock(void *context);

#define MAX_EVENTS 16
typedef struct {
    event_t events[MAX_EVENTS];
    uint8_t front_idx;
    uint8_t rear_idx;
    uint8_t size;
} event_queue_t;

static event_queue_t event_queue = { 0 };

void queue_event(event_t event)
{
    // Pushes to rear
    event_queue.events[event_queue.rear_idx] = event;
    event_queue.rear_idx = (event_queue.rear_idx + 1) % MAX_EVENTS;
    event_queue.size += 1;
}

static bool pop_event(event_t *out_event)
{
    if (event_queue.size == 0) {
        return false;
    }

    // Pops from front 
    *out_event = event_queue.events[event_queue.front_idx];
    event_queue.front_idx = (event_queue.front_idx + 1) % MAX_EVENTS;
    event_queue.size -= 1;

    return true;
}

/*
 * Event handling
 */

void window_changed_size(saver_state_t *state, XConfigureEvent *event)
{
    state->canvas_width = event->width;
    state->canvas_height = event->height;

    cairo_xlib_surface_set_size(state->surface, event->width, event->height);

    // Mark all layers as dirty. 
    set_layer_needs_draw(state, ALL_LAYERS, true);
}

void handle_event(saver_state_t *state, event_t event)
{
    char *password_buf = state->password_buffer;
    const size_t pw_len = strlen(password_buf);

    switch (event.type) {
        case EVENT_KEYBOARD_BACKSPACE:
            if (state->input_allowed && pw_len > 0) {
                password_buf[pw_len - 1] = '\0';
            }
            break;
        case EVENT_KEYBOARD_RETURN:
            if (state->input_allowed) {
                accept_password(state);
            }
            break;
        case EVENT_KEYBOARD_CLEAR:
            if (state->input_allowed) {
                clear_password(state);
            }
            break;
        case EVENT_KEYBOARD_LETTER:
            if (state->input_allowed && pw_len + 1 < kMaxPasswordLength) {
                password_buf[pw_len] = event.codepoint; // TODO: does not handle unicode correctly. 
                password_buf[pw_len + 1] = '\0';
            }
            break;
        case EVENT_SURFACE_SIZE_CHANGED:
            fprintf(stderr, "Got surface size changed event\n");
            set_layer_needs_draw(state, ALL_LAYERS, true);
        default:
            break;
    }

    reset_cursor_flash_anim(state);
    set_layer_needs_draw(state, LAYER_PASSWORD, true);
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
    state->input_allowed = false;

    // Schedule a timer to show the "Authenticating..." UI after some time 
    saver_timer_t timer;
    timer.exec_time = anim_now() + 0.5;
    timer.callback = callback_show_auth_progress;
    state->show_spinner_timer = push_timer(state, &timer);
}

static void reset_cursor_flash_anim(saver_state_t *state) 
{
    animation_t *cursor_anim = get_animation_for_key(state, state->cursor_anim_key);
    if (cursor_anim) {
        cursor_anim->start_time = anim_now() + 0.5;
        cursor_anim->direction = OUT;
    }
}

static void ending_animation_completed(struct animation_t *animation, void *context)
{
    saver_state_t *state = saver_state(context);
    state->is_authenticated = true;
}

static void authentication_accepted(saver_state_t *state)
{
    // Cancel timer to show spinner
    cancel_timer(state, state->show_spinner_timer);

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
    saver_state_t *state = saver_state(context);
    set_password_prompt(state, info_msg);
    set_layer_needs_draw(state, LAYER_PROMPT, true);
}

void callback_show_error(const char *error_msg, void *context)
{
    saver_state_t *state = saver_state(context);
    set_password_prompt(state, error_msg);
    set_layer_needs_draw(state, LAYER_PROMPT, true);
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

void callback_show_auth_progress(void *context)
{
    saver_state_t *state = saver_state(context);

    // Spinner animation
    state->is_processing = true;
    if (state->spinner_anim_key == ANIM_KEY_NOEXIST) {
        state->spinner_anim_key = schedule_animation(state, (animation_t) {
            .type = ASpinnerAnimation,
            .anim.spinner_anim = { 0 }
        });
    }

    // Update prompt
    set_password_prompt(state, "Authenticating...");
}

void callback_update_clock(void *context)
{
    saver_state_t *state = saver_state(context);

    time_t n_time = time(NULL);
    struct tm *now = localtime(&n_time);
    snprintf(state->clock_str, kMaxClockLength, "%.2d:%.2d:%.2d", 
        now->tm_hour, now->tm_min, now->tm_sec);

    set_layer_needs_draw(state, LAYER_CLOCK | LAYER_LOGO, true);
    reset_timer(state, state->clock_update_timer_id, 1.0);
}

/*
 * Timers
 */

timer_id push_timer(saver_state_t *state, saver_timer_t *timer)
{
    unsigned int slot = 0;
    for (unsigned int i = 0; i < kMaxTimers; i++) {
        saver_timer_t *timer_slot = &state->timers[i];
        if (timer_slot->active == false) {
            slot = i;
            timer->active = true;
            memcpy(timer_slot, timer, sizeof (saver_timer_t));
            
            break;
        }
    }

    return slot;
}

void reset_timer(saver_state_t *state, timer_id timerid, anim_time_interval_t duration)
{
    saver_timer_t *timer = &state->timers[timerid];
    timer->exec_time = anim_now() + duration;
    timer->active = true;
}

void cancel_timer(saver_state_t *state, timer_id timerid)
{
    saver_timer_t *timer = &state->timers[timerid];
    timer->active = false;
}


/*
 * Main drawing/update routines
 */

static void handle_pending_events(saver_state_t *state)
{
    event_t event = { 0 };
    if (pop_event(&event)) {
        handle_event(state, event);
    }
}

static void draw(saver_state_t *state)
{
    if (layer_needs_draw(state, LAYER_BACKGROUND)) {
        draw_background(state, 0, 0, state->canvas_width, state->canvas_height);
    }

    if (layer_needs_draw(state, LAYER_LOGO)) {
        draw_logo(state);
    }

    if (state->clock_enabled && layer_needs_draw(state, LAYER_CLOCK)) {
        draw_clock(state);
    }

    draw_password_field(state);

    // Automatically reset this after every draw call
    set_layer_needs_draw(state, LAYER_BACKGROUND, false);
}

static void timers(saver_state_t *state)
{
    anim_time_interval_t now = anim_now();
    for (unsigned int i = 0; i < kMaxTimers; i++) {
        saver_timer_t *timer = &state->timers[i];
        if (timer->active && now > timer->exec_time) {
            timer->active = false;
            timer->callback((struct saver_state_t *)state);
        }
    }
}

static int runloop(saver_state_t *state)
{
    // Main run loop
    const display_server_interface_t *interface = display_server_get_interface();
    while (!state->is_authenticated) {
        update_animations(state);
        interface->poll_events(state);
        handle_pending_events(state);

        cairo_push_group(state->ctx);
        
        draw(state);
        
        cairo_pop_group_to_source(state->ctx);

        cairo_paint(state->ctx);
        cairo_surface_flush(state->surface);

        interface->commit_surface();

        timers(state);

        interface->await_frame();
    }

    if (state->is_authenticated) {
        // If we exited the main loop and we successfully authenticated, post this to our display server. 
        interface->unlock_session();
    }

    // Cleanup
    cairo_destroy(state->ctx);

    return EXIT_SUCCESS;
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

void print_usage(const char *progname) 
{
    fprintf(stderr, "Usage: %s [OPTION]\n", progname);
    fprintf(stderr, "buzzert's screen locker for Wayland/XSecureLock.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h   Show this help message.\n");
    fprintf(stderr, "  -c   Show a clock on the lock screen (%s).\n", kEnableClockEnvVar);
}

int main(int argc, char **argv)
{
    bool enable_clock = getenv(kEnableClockEnvVar) != NULL;

    int opt;
    while ((opt = getopt(argc, argv, "ch")) != -1) {
        switch (opt) {
            case 'c':
                enable_clock = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                break;
        }
    }
    
    // Initialize display server backend
    if (!display_server_init()) {
        fprintf(stderr, "Error initializing display server\n");
        exit(1);
    }
    
    const display_server_interface_t *interface = display_server_get_interface();
    if (!interface) {
        fprintf(stderr, "Error getting display server interface\n");
        exit(1);
    }
    
    cairo_surface_t *surface = interface->acquire_surface();
    if (surface == NULL) {
        fprintf(stderr, "Error creating cairo surface\n");
        exit(1);
    }

    // Make it so reading from the xsecurelock file descriptor doesn't block
    int flags = fcntl(kXSecureLockCharFD, F_GETFL, 0);
    fcntl(kXSecureLockCharFD, F_SETFL, flags | O_NONBLOCK);

    // Initialize Cairo
    cairo_t *cr = cairo_create(surface);

    // Initialize pango context
    PangoLayout *pango_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *status_font = pango_font_description_from_string(kDefaultFont);
    PangoFontDescription *clock_font = pango_font_description_from_string(kClockFont);

    saver_state_t state = { 0 };
    state.ctx = cr;
    state.surface = surface;
    state.cursor_opacity = 1.0;
    state.pango_layout = pango_layout;
    state.status_font = status_font;
    state.clock_font = clock_font;
    state.clock_enabled = enable_clock;
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

    // Clock update timer
    if (enable_clock) {
        saver_timer_t clock_update_timer;
        clock_update_timer.exec_time = anim_now() + 1.0;
        clock_update_timer.callback = callback_update_clock;
        state.clock_update_timer_id = push_timer(&state, &clock_update_timer);
        callback_update_clock(&state);
    }

    display_bounds_t bounds;
    interface->get_display_bounds(get_preferred_monitor_num(), &bounds);
    state.canvas_width = bounds.width;
    state.canvas_height = bounds.height;

    // Docs say this must be called whenever the size of the window changes (X11 only)
    if (display_server_get_type() == DISPLAY_SERVER_X11) {
        cairo_xlib_surface_set_size(surface, state.canvas_width, state.canvas_height);
    }

    auth_callbacks_t callbacks = {
        .info_handler = callback_show_info,
        .error_handler = callback_show_error,
        .prompt_handler = callback_prompt_user,
        .result_handler = callback_authentication_result
    };

    state.auth_handle = auth_begin_authentication(callbacks, &state);

    int result = runloop(&state);

    interface->destroy_surface(surface);
    interface->cleanup();
    return result;
}

