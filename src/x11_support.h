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

// Sets up a window and returns a cairo_surface to draw onto
cairo_surface_t* x11_helper_acquire_cairo_surface(int width, int height);

// Cleanup
void x11_helper_destroy_surface(cairo_surface_t *surface);

