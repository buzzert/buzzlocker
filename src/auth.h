/*
 * auth.h
 *
 * Handles all authentication events via PAM
 * Created by buzzert <buzzert@buzzert.net> 2019-01-19
 */

#pragma once

#define MAX_RESPONSE_SIZE 128

typedef struct {
    char  response_buffer[MAX_RESPONSE_SIZE];
    int   response_code;
} auth_prompt_response_t;

// NOTE: These callbacks are called on a separate thread
typedef void(*ShowInfo)(const char *info_msg, void *context);
typedef void(*ShowError)(const char *error_msg, void *context);
typedef void(*PromptUser)(const char *prompt, void *context);
typedef void(*AuthenticationResult)(int result, void *context);

typedef struct {
    ShowInfo             info_handler;
    ShowError            error_handler;
    PromptUser           prompt_handler;
    AuthenticationResult result_handler;
} auth_callbacks_t;

struct auth_handle_t;

// Starts an authentication thread and returns immediately
struct auth_handle_t* auth_begin_authentication(auth_callbacks_t callbacks, void *context);

// Perform an authentication attempt
void auth_attempt_authentication(struct auth_handle_t *handle, auth_prompt_response_t response);

