/*
 * x11_support.h
 *
 * Relevant helper functions for acquiring a drawing surface on X11
 * Created by buzzert <buzzert@buzzert.net> 2019-01-18
 */

#pragma once 

#include <cairo/cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef struct {
    int x;
    int y;
    int width;
    int height;
} x11_display_bounds_t;

// Get the preferred monitor number (via BUZZLOCKER_MONITOR_NUM environment variable)
// Returns 0 (the primary one) if not set 
unsigned int get_preferred_monitor_num();

// Get the bounds for the specified monitor num (via XRandR)
void x11_get_display_bounds(unsigned int monitor_num, x11_display_bounds_t *out_bounds);

// Sets up a window and returns a cairo_surface to draw onto
cairo_surface_t* x11_helper_acquire_cairo_surface();

// Cleanup
void x11_helper_destroy_surface(cairo_surface_t *surface);

