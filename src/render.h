/*
 * render.h
 *
 * Created by buzzert <buzzzer@buzzert.net> 2019-01-18
 */

#pragma once

#include <cairo/cairo.h>
#include <cairo-xlib.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

typedef struct {
    cairo_t                *ctx;
    cairo_surface_t        *surface;

    PangoLayout            *pango_layout;
    PangoFontDescription   *status_font;

    RsvgHandle             *logo_svg_handle;
    RsvgHandle             *asterisk_svg_handle;

    int                     canvas_width;
    int                     canvas_height;

    bool                    cursor_animating;
    double                  cursor_opacity;
    double                  cursor_fade_direction;

    char                   *password_buffer;
    size_t                  password_buffer_len;
} saver_state_t;

// The purple sidebar
void draw_logo(saver_state_t *state);

// The status string and paassword field
void draw_password_field(saver_state_t *state);


