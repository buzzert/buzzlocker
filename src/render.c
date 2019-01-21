/*
 * render.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-18
 */

#include "render.h"
#include "resources.h"

#include <assert.h>
#include <gio/gio.h>

static const double kLogoBackgroundWidth = 500.0;

GBytes* get_data_for_resource(const char *resource_path)
{
    GBytes *result = NULL;
    GError *error = NULL;

    GResource *resource = as_get_resource();
    result = g_resource_lookup_data(
        resource,
        resource_path,
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error
    );

    if (error != NULL) {
        fprintf(stderr, "Error loading resource %s\n", resource_path);
    }

    return result;
}

static void update_single_animation(saver_state_t *state, animation_t *anim)
{
    // Cursor animation
    if (anim->anim.type == ACursorAnimation) {
        CursorAnimation *ca = &anim->anim.cursor_anim;

        if (ca->cursor_animating) {
            const double cursor_fade_speed = 0.05;
            if (ca->cursor_fade_direction > 0) {
                state->cursor_opacity += cursor_fade_speed;
                if (state->cursor_opacity > 1.0) {
                    ca->cursor_fade_direction *= -1;
                }
            } else {
                state->cursor_opacity -= cursor_fade_speed;
                if (state->cursor_opacity <= 0.0) {
                    ca->cursor_fade_direction *= -1;
                }
            }
        } else {
            state->cursor_opacity = 1.0;
        }
    }

    // Logo animation
    else if (anim->anim.type == ALogoAnimation) {
        const double logo_duration = 0.6;

        anim_time_interval_t now = anim_now();
        double progress = (now - anim->start_time) / logo_duration;

        state->logo_fill_progress = anim_qubic_ease_out(progress);
        if (anim->anim.logo_anim.direction) {
            state->logo_fill_progress = 1.0 - anim_qubic_ease_out(progress);
        }

        bool completed = (state->logo_fill_progress >= 1.0);
        if (anim->anim.logo_anim.direction) {
            completed = (state->logo_fill_progress <= 0.0);
        }

        anim->completed = completed;
    }
}

static unsigned next_anim_index(saver_state_t *state, unsigned cur_idx)
{
    unsigned idx = cur_idx + 1;
    for (; idx < kMaxAnimations; idx++) {
        animation_t anim = state->animations[idx];
        if (anim.anim.type != _EmptyAnimationType) break;
    }

    return idx;
}

void schedule_animation(saver_state_t *state, animation_t anim)
{
    anim.start_time = anim_now();

    // Find next empty element
    for (unsigned idx = 0; idx < kMaxAnimations; idx++) {
        animation_t check_anim = state->animations[idx];
        if (check_anim.anim.type == _EmptyAnimationType) {
            state->animations[idx] = anim;
            state->num_animations++;
            break;
        }
    }
}

void update_animations(saver_state_t *state)
{
    unsigned idx = 0;
    unsigned processed_animations = 0;
    unsigned completed_animations = 0;
    while (processed_animations < state->num_animations) {
        animation_t *anim = &state->animations[idx];

        update_single_animation(state, anim);
        if (anim->completed) {
            state->animations[idx].anim.type = _EmptyAnimationType;
            if (anim->completion_func != NULL) {
                anim->completion_func((struct animation_t *)anim, anim->completion_func_context);
            }

            completed_animations++;
        }

        processed_animations++;
        idx = next_anim_index(state, idx);
        if (idx == kMaxAnimations) break;
    }

    state->num_animations -= completed_animations;
}

void draw_logo(saver_state_t *state)
{
    if (state->logo_svg_handle == NULL) {
        GError *error = NULL;
        GBytes *bytes = get_data_for_resource("/resources/logo.svg");

        gsize size = 0;
        gconstpointer data = g_bytes_get_data(bytes, &size);
        state->logo_svg_handle = rsvg_handle_new_from_data(data, size, &error);
        g_bytes_unref(bytes);
        if (error != NULL) {
            fprintf(stderr, "Error loading logo SVG\n");
            return;
        }
    }


    cairo_t *cr = state->ctx;

    cairo_save(cr);
    cairo_set_source_rgb(cr, (208.0 / 255.0), (69.0 / 255.0), (255.0 / 255.0));
    double fill_height = (state->canvas_height * state->logo_fill_progress);
    cairo_rectangle(cr, 0, 0, kLogoBackgroundWidth, fill_height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    
    // Scale and draw logo
    RsvgDimensionData dimensions;
    rsvg_handle_get_dimensions(state->logo_svg_handle, &dimensions);

    const double padding = 100.0;
    double scale_factor = ((kLogoBackgroundWidth - (padding * 2.0)) / dimensions.width);
    double scaled_height = (dimensions.height * scale_factor);
    double y_position = (state->canvas_height - scaled_height) / 2.0;
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
    const double field_y = (state->canvas_height - cursor_height) / 2.0;
    const double field_padding = 10.0;
    
    cairo_t *cr = state->ctx;

    // Draw status text
    const char *prompt = (state->password_prompt ?: "???");
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    pango_layout_set_font_description(state->pango_layout, state->status_font);
    pango_layout_set_text(state->pango_layout, prompt, -1);

    int t_width, t_height;
    pango_layout_get_size(state->pango_layout, &t_width, &t_height);
    double line_height = t_height / PANGO_SCALE;

    cairo_move_to(cr, field_x, field_y - line_height - field_padding);
    pango_cairo_show_layout(cr, state->pango_layout);

    // Draw password asterisks
    if (state->asterisk_svg_handle == NULL) {
        GError *error = NULL;
        GBytes *bytes = get_data_for_resource("/resources/asterisk.svg");

        gsize size = 0;
        gconstpointer data = g_bytes_get_data(bytes, &size);
        state->asterisk_svg_handle = rsvg_handle_new_from_data(data, size, &error);
        g_bytes_unref(bytes);
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
    if (!state->is_processing) {
        cairo_rectangle(cr, field_x + cursor_offset_x, field_y, cursor_width, cursor_height);
    } else {
        // Fill asterisks
        cairo_rectangle(cr, field_x, field_y, cursor_offset_x, cursor_height);
    }
    cairo_fill(cr);

}

