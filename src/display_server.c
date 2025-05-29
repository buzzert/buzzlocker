/*
 * display_server.c
 * 
 * Display server abstraction implementation
 * Created by Claude Code
 */

#include "display_server.h"

#include <stdlib.h>
#include <stdio.h>

// Forward declarations for backend interfaces
extern const display_server_interface_t x11_interface;
extern const display_server_interface_t wayland_interface;

static display_server_type_t current_display_server = DISPLAY_SERVER_X11;
static const display_server_interface_t *current_interface = NULL;

display_server_type_t display_server_detect(void)
{
    // Check for Wayland first
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display != NULL && wayland_display[0] != '\0') {
        return DISPLAY_SERVER_WAYLAND;
    }
    
    // Fallback to X11
    return DISPLAY_SERVER_X11;
}

bool display_server_init(void)
{
    current_display_server = display_server_detect();
    
    switch (current_display_server) {
        case DISPLAY_SERVER_X11:
            current_interface = &x11_interface;
            break;
        case DISPLAY_SERVER_WAYLAND:
            current_interface = &wayland_interface;
            break;
        default:
            fprintf(stderr, "Unknown display server type\n");
            return false;
    }
    
    if (current_interface && current_interface->init) {
        return current_interface->init();
    }
    
    return true;
}

display_server_type_t display_server_get_type(void)
{
    return current_display_server;
}

const display_server_interface_t* display_server_get_interface(void)
{
    return current_interface;
}