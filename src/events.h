/*
 * events.h
 * 
 * Created 2025-05-28 by James Magahern <james@magahern.com>
 */

#pragma once

#include <stdlib.h>

typedef enum {
    EVENT_SURFACE_SIZE_CHANGED,
    EVENT_KEYBOARD_LETTER,
    EVENT_KEYBOARD_RETURN,
    EVENT_KEYBOARD_CLEAR,
    EVENT_KEYBOARD_BACKSPACE,
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t codepoint;
} event_t;

void queue_event(event_t event);

// X11 support functions
unsigned int get_preferred_monitor_num(void);