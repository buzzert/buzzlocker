/*
 * render.h
 *
 * Created by buzzert <buzzzer@buzzert.net> 2019-01-18
 */

#pragma once

#include "animation.h"
#include "auth.h"

#include <cairo/cairo.h>
#include <cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

#define kMaxAnimations 32
#define kMaxPasswordLength 128
#define kMaxPromptLength   128
#define kMaxTimers         16

typedef unsigned animation_key_t;
#define ANIM_KEY_NOEXIST (kMaxAnimations + 1)

typedef enum {
    LAYER_BACKGROUND     = 1 << 0,
    LAYER_PROMPT         = 1 << 1,
    LAYER_LOGO           = 1 << 2,
    LAYER_PASSWORD       = 1 << 3,
} layer_type_t;


typedef unsigned int timer_id;
typedef void (*timer_callback_t)(void *context);
typedef struct {
    bool                    active;
    anim_time_interval_t    exec_time;
    timer_callback_t        callback;
} saver_timer_t;

typedef struct {
    cairo_t                *ctx;
    cairo_surface_t        *surface;

    PangoLayout            *pango_layout;
    PangoFontDescription   *status_font;

    double                  background_redshift;

    RsvgHandle             *logo_svg_handle;
    double                  logo_fill_width;
    double                  logo_fill_height;

    RsvgHandle             *asterisk_svg_handle;

    int                     canvas_width;
    int                     canvas_height;

    bool                    input_allowed;
    double                  cursor_opacity;
    animation_key_t         cursor_anim_key;

    bool                    is_processing;
    bool                    is_authenticated;

    timer_id                show_spinner_timer;
    RsvgHandle             *spinner_svg_handle;
    animation_key_t         spinner_anim_key;

    char                    password_prompt[kMaxPromptLength];
    char                    password_buffer[kMaxPasswordLength];
    double                  password_opacity;

    animation_t             animations[kMaxAnimations];
    unsigned                num_animations;

    saver_timer_t           timers[kMaxTimers];

    layer_type_t            dirty_layers;

    struct auth_handle_t   *auth_handle;
} saver_state_t;

// Use this to set the prompt ("Password: ")
void set_password_prompt(saver_state_t *state, const char *prompt);

// Start an animation
animation_key_t schedule_animation(saver_state_t *state, animation_t anim);

// Stop an animation
void remove_animation(saver_state_t *state, animation_key_t anim_key);

// Get a running animation (returns NULL if it doesn't exist)
animation_t* get_animation_for_key(saver_state_t *state, animation_key_t anim_key);

// Update all running animations
void update_animations(saver_state_t *state);

// Background
void draw_background(saver_state_t *state, double x, double y, double width, double height);

// The purple sidebar
void draw_logo(saver_state_t *state);

// The status string and paassword field
void draw_password_field(saver_state_t *state);

// Convenience function for getting layer dirty state
bool layer_needs_draw(saver_state_t *state, const layer_type_t type);

// Convenience function for setting layer dirty state
void set_layer_needs_draw(saver_state_t *state, const layer_type_t type, bool needs_draw);
