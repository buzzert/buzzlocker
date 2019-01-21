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

typedef struct {
    cairo_t                *ctx;
    cairo_surface_t        *surface;

    PangoLayout            *pango_layout;
    PangoFontDescription   *status_font;

    RsvgHandle             *logo_svg_handle;
    double                  logo_fill_progress;

    RsvgHandle             *asterisk_svg_handle;

    int                     canvas_width;
    int                     canvas_height;

    bool                    input_allowed;
    double                  cursor_opacity;

    bool                    is_processing;
    bool                    is_authenticated;
    const char             *password_prompt;
    char                   *password_buffer;
    size_t                  password_buffer_len;

    animation_t             animations[kMaxAnimations];
    unsigned                num_animations;

    struct auth_handle_t   *auth_handle;
} saver_state_t;

// Start an animation
void schedule_animation(saver_state_t *state, animation_t anim);

// Update all running animations
void update_animations(saver_state_t *state);

// The purple sidebar
void draw_logo(saver_state_t *state);

// The status string and paassword field
void draw_password_field(saver_state_t *state);


