/*
 * render.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-18
 */

#include "render.h"
#include "resources.h"

#include <assert.h>
#include <gio/gio.h>
#include <math.h>

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

void set_password_prompt(saver_state_t *state, const char *prompt)
{
    fprintf(stderr, "Prompt: %s\n", prompt);
    strncpy(state->password_prompt, prompt, kMaxPromptLength - 1);
}

static void update_single_animation(saver_state_t *state, animation_t *anim)
{
    // Cursor animation
    if (anim->type == ACursorAnimation) {
        CursorAnimation *ca = &anim->anim.cursor_anim;

        if (ca->cursor_animating) {
            if (!state->is_processing) {
                const double fade_duration = 0.5;
                const double progress = anim_progress(anim, fade_duration);

                state->cursor_opacity = progress;

                if (anim_complete(anim, progress)) {
                    anim->direction = !anim->direction;
                    anim->start_time = anim_now();
                }
            } else {
                state->cursor_opacity = 1.0;
            }
        }
    }

    // Logo animation
    else if (anim->type == ALogoAnimation) {
        const double logo_duration = 0.6;
        const double progress = anim_progress_ease(anim, logo_duration, anim_qubic_ease_out);

        if (anim->direction == IN) {
            state->logo_fill_height = progress;
            state->logo_fill_width = 1.0;
        } else {
            state->logo_fill_width = progress;
            state->logo_fill_height = 1.0;
        }

        state->password_opacity = progress;
        set_layer_needs_draw(state, LAYER_LOGO, true);
        if (anim->direction == OUT) {
            // When transitioning OUT, background essentially draws over the logo as it wipes out
            set_layer_needs_draw(state, LAYER_BACKGROUND, true);
        }

        // And since the status text fades along with the logo
        set_layer_needs_draw(state, LAYER_PROMPT, true);

        anim->completed = anim_complete(anim, progress);
    }

    // Background red flash animation
    else if (anim->type == ARedFlashAnimation) {
        const double duration = 0.1;
        const double progress = anim_progress_ease(anim, duration, anim_qubic_ease_out);

        bool completed = false;
        if (anim_complete(anim, progress)) {
            AnimationDirection direction = anim->direction;
            anim->direction = !direction;
            anim->anim.redflash_anim.flash_count++;
            anim->start_time = anim_now();
            if (anim->anim.redflash_anim.flash_count > 3) {
                completed = true;
            }
        }

        anim->completed = completed;
        state->background_redshift = progress;
        set_layer_needs_draw(state, LAYER_BACKGROUND, true);
    }

    // Spinner animation
    else if (anim->type == ASpinnerAnimation) {
        anim->anim.spinner_anim.rotation += 0.07;
    }
}

static unsigned next_anim_index(saver_state_t *state, unsigned cur_idx)
{
    unsigned idx = cur_idx + 1;
    for (; idx < kMaxAnimations; idx++) {
        animation_t anim = state->animations[idx];
        if (anim.type != _EmptyAnimationType) break;
    }

    return idx;
}

animation_key_t schedule_animation(saver_state_t *state, animation_t anim)
{
    anim.start_time = anim_now();

    // Find next empty element
    animation_key_t key = 0;
    for (unsigned idx = 0; idx < kMaxAnimations; idx++) {
        animation_t check_anim = state->animations[idx];
        if (check_anim.type == _EmptyAnimationType) {
            key = idx;
            state->animations[idx] = anim;
            state->num_animations++;
            break;
        }
    }

    return key;
}

void remove_animation(saver_state_t *state, animation_key_t key)
{
    state->animations[key].type = _EmptyAnimationType;
}

animation_t* get_animation_for_key(saver_state_t *state, animation_key_t anim_key)
{
    animation_t *animation = NULL;
    if (state->animations[anim_key].type != _EmptyAnimationType) {
        animation = &state->animations[anim_key];
    }

    return animation;
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
            remove_animation(state, idx);
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

bool layer_needs_draw(saver_state_t *state, const layer_type_t type)
{
    if (state->dirty_layers & LAYER_BACKGROUND) {
        // Special case: if the background needs to be drawn, everything on top of it
        // needs to as well.
        return true;
    }

    return (state->dirty_layers & type);
}

void set_layer_needs_draw(saver_state_t *state, const layer_type_t type, bool needs_draw)
{
    if (needs_draw) {
        state->dirty_layers |= type;
    } else {
        state->dirty_layers &= ~type;
    }
}

RsvgHandle* load_svg_for_resource_path(const char *resource_path)
{
    GError *error = NULL;
    GBytes *bytes = get_data_for_resource(resource_path);
    RsvgHandle *handle = NULL;

    gsize size = 0;
    gconstpointer data = g_bytes_get_data(bytes, &size);
    handle = rsvg_handle_new_from_data(data, size, &error);
    g_bytes_unref(bytes);
    if (error != NULL) {
        fprintf(stderr, "Error loading SVG at resource path: %s\n", resource_path);
    }

    return handle;
}

void draw_background(saver_state_t *state, double x, double y, double width, double height)
{
    // Draw background
    cairo_t *cr = state->ctx;
    cairo_save(cr);
    cairo_set_source_rgba(cr, (state->background_redshift / 1.5), 0.0, 0.0, 1.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);
    cairo_restore(cr);
}

void draw_logo(saver_state_t *state)
{
    if (state->logo_svg_handle == NULL) {
        state->logo_svg_handle = load_svg_for_resource_path("/resources/logo.svg");
    }

    cairo_t *cr = state->ctx;

    // Draw bar background
    cairo_save(cr);
    cairo_set_source_rgb(cr, (208.0 / 255.0), (69.0 / 255.0), (255.0 / 255.0));
    double fill_height = (state->canvas_height * state->logo_fill_height);
    double fill_width = (kLogoBackgroundWidth * state->logo_fill_width);
    cairo_rectangle(cr, 0, 0, fill_width, fill_height);
    cairo_fill(cr);

    // Common color -- transparent for logo
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

    set_layer_needs_draw(state, LAYER_LOGO, false);
}

void draw_password_field(saver_state_t *state)
{
    const double cursor_height = 40.0;
    const double cursor_width  = 30.0;
    const double field_x = kLogoBackgroundWidth + 50.0;
    const double field_y = (state->canvas_height - cursor_height) / 2.0;
    const double field_padding = 10.0;
    
    cairo_t *cr = state->ctx;

    // Common color for status and password field
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, state->password_opacity);

    // Measure status text
    const char *prompt = state->password_prompt;
    pango_layout_set_font_description(state->pango_layout, state->status_font);
    pango_layout_set_text(state->pango_layout, prompt, -1);

    int t_width, t_height;
    pango_layout_get_size(state->pango_layout, &t_width, &t_height);
    double line_height = t_height / PANGO_SCALE;

    // Measure processing indicator
    double spinner_width = 0.0;
    double spinner_scale_factor = 0.0;
    RsvgDimensionData spinner_dimensions;
    if (state->is_processing) {
        if (state->spinner_svg_handle == NULL) {
            state->spinner_svg_handle = load_svg_for_resource_path("/resources/spinner.svg");
        }

        rsvg_handle_get_dimensions(state->spinner_svg_handle, &spinner_dimensions);
        spinner_scale_factor = ((line_height - 5.0) / spinner_dimensions.height);
        spinner_width = spinner_dimensions.width * spinner_scale_factor;

        // padding
        spinner_width += 10.0;
    }

    // Draw status text
    if (layer_needs_draw(state, LAYER_PROMPT) || state->is_processing) {
        const double y_position = field_y - line_height - field_padding;
        draw_background(state, field_x, y_position, state->canvas_width - field_x, line_height);
        cairo_move_to(cr, spinner_width + field_x, y_position);
        pango_cairo_show_layout(cr, state->pango_layout);

        set_layer_needs_draw(state, LAYER_PROMPT, false);
    }

    // Draw processing indicator
    if (state->is_processing) {
        SpinnerAnimation spinner_anim = get_animation_for_key(state, state->spinner_anim_key)->anim.spinner_anim;

        cairo_save(cr);

        cairo_translate(cr, field_x, field_y - line_height - 8.0);

        // Translate, rotate, translate; so rotation is happening about the center.
        double tr_amount = (spinner_dimensions.width * spinner_scale_factor) / 2.0;
        cairo_translate(cr, tr_amount, tr_amount);
        cairo_rotate(cr, spinner_anim.rotation);
        cairo_translate(cr, -tr_amount, -tr_amount);

        cairo_scale(cr, spinner_scale_factor, spinner_scale_factor);

        rsvg_handle_render_cairo(state->spinner_svg_handle, cr);

        cairo_restore(cr);
    }

    // Draw password asterisks
    const double cursor_padding_x = 10.0;
    if (state->asterisk_svg_handle == NULL) {
        state->asterisk_svg_handle = load_svg_for_resource_path("/resources/asterisk.svg");
    }

    RsvgDimensionData dimensions;
    rsvg_handle_get_dimensions(state->asterisk_svg_handle, &dimensions);
    
    const double asterisk_height = cursor_height - 20.0;
    const double scale_factor = (asterisk_height / dimensions.height);
    const double scaled_width = (dimensions.width * scale_factor);
    const double asterisk_width = (scaled_width + cursor_padding_x);
    const unsigned int num_asterisks = strlen(state->password_buffer);

    if (layer_needs_draw(state, LAYER_PASSWORD)) {
        // Draw background first
        draw_background(state, field_x, field_y - (field_padding / 2.0), 
                        (asterisk_width * num_asterisks), cursor_height + field_padding);

        // Asterisks are all rendered in a single group so their opacity can change (password_opacity)
        cairo_push_group(cr);
        double cursor_offset_x = 0.0;
        for (unsigned i = 0; i < num_asterisks; i++) {
            cairo_save(cr);
            cairo_translate(cr, field_x + cursor_offset_x, field_y + ((cursor_height - asterisk_height) / 2.0));
            cairo_scale(cr, scale_factor, scale_factor);
            rsvg_handle_render_cairo(state->asterisk_svg_handle, cr);
            cairo_restore(cr);

            cursor_offset_x += asterisk_width;
        }

        cairo_pattern_t *asterisk_pattern = cairo_pop_group(cr);

        cairo_save(cr);
        cairo_set_source(cr, asterisk_pattern);
        cairo_paint_with_alpha(cr, state->password_opacity);
        cairo_restore(cr);
        cairo_pattern_destroy(asterisk_pattern);

        set_layer_needs_draw(state, LAYER_PASSWORD, false);
    }

    // Draw cursor
    const double x_offset = (num_asterisks * asterisk_width);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, MIN(state->password_opacity, state->cursor_opacity));
    draw_background(state, field_x + x_offset, field_y, state->canvas_width, cursor_height);
    if (!state->is_processing) {
        cairo_rectangle(cr, field_x + x_offset, field_y, cursor_width, cursor_height);
    } else {
        // Fill asterisks
        cairo_rectangle(cr, field_x, field_y, x_offset, cursor_height);
    }
    cairo_fill(cr);
}

