/*
 * display_server.h
 * 
 * Display server abstraction for X11 and Wayland backends
 * Created 2025-05-28 by James Magahern <james@magahern.com>
 */

#pragma once

#include <cairo/cairo.h>
#include <stdbool.h>

typedef enum {
    DISPLAY_SERVER_X11,
    DISPLAY_SERVER_WAYLAND
} display_server_type_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} display_bounds_t;

// Backend interface
typedef struct display_server_interface {
    // Initialize the display server connection
    bool (*init)(void);
    
    // Create and setup the lock screen surface(s)
    cairo_surface_t* (*acquire_surface)(void);
    
    // Get display bounds for the specified monitor
    void (*get_display_bounds)(unsigned int monitor_num, display_bounds_t *bounds);
    
    // Poll for events (keyboard, resize, etc.) 
    void (*poll_events)(void *state);
    
    // Commit surface changes 
    void (*commit_surface)(void);
    
    // Unlock session (must call once auth is complete)
    void (*unlock_session)(void);

    // If applicable, waits for the next frame to be available for commit. 
    void (*await_frame)(void);
    
    // Cleanup resources
    void (*destroy_surface)(cairo_surface_t *surface);
    
    // Cleanup display server connection
    void (*cleanup)(void);
    
} display_server_interface_t;

// Initialize the appropriate display server backend
bool display_server_init(void);

// Get the current display server type
display_server_type_t display_server_get_type(void);

// Get the current backend interface
const display_server_interface_t* display_server_get_interface(void);

// Detect which display server to use based on environment
display_server_type_t display_server_detect(void);
