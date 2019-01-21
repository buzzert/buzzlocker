/*
 * auth.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-19
 */

#include "auth.h"

#include <pthread.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct auth_handle_t {
    void             *context;
    auth_callbacks_t  callbacks;

    sem_t                  prompt_semaphore;
    auth_prompt_response_t prompt_response;
};

int process_message(const struct pam_message *msg, struct pam_response *resp, struct auth_handle_t *handle)
{
    switch (msg->msg_style) {
        case PAM_PROMPT_ECHO_ON:
        case PAM_PROMPT_ECHO_OFF: {
            handle->callbacks.prompt_handler(msg->msg, handle->context);

            sem_wait(&handle->prompt_semaphore);

            auth_prompt_response_t response = handle->prompt_response;

            // resp is freed by libpam
            resp->resp = malloc(MAX_RESPONSE_SIZE);
            strncpy(resp->resp, response.response_buffer, MAX_RESPONSE_SIZE);
            resp->resp_retcode = 0; // docs say this should always be zero
            break;
        }
        case PAM_ERROR_MSG:
            handle->callbacks.error_handler(msg->msg, handle->context);
            break;
        case PAM_TEXT_INFO:
            handle->callbacks.info_handler(msg->msg, handle->context);
            break;
    }

    return PAM_SUCCESS;
}

int perform_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *data)
{
	*resp = calloc(num_msg, sizeof(struct pam_response));

    struct auth_handle_t *handle = (struct auth_handle_t *)data;
    for (unsigned i = 0; i < num_msg; i++) {
        process_message(&(( *msg )[i]), & (( *resp )[i]), handle);
    }

    return PAM_SUCCESS;
}

static void* auth_thread_main(void *arg)
{
    struct pam_conv conv;
    conv.conv = perform_conversation;
    conv.appdata_ptr = arg;

    // Get current username
    struct passwd *pwd = getpwuid(getuid());
    const char *username = pwd->pw_name;
    if (username == NULL || strlen(username) == 0) {
        fprintf(stderr, "Couldn't get name for the current user\n");
        // todo: report to callback
    }

    // Start PAM authentication
    pam_handle_t *pam = NULL;
    pam_start(
        "login",
        username,
        &conv,
        &pam
    );

    bool authenticating = true;
    struct auth_handle_t *handle = (struct auth_handle_t *)arg;
    while (authenticating) {
        int status = pam_authenticate(pam, 0);
        handle->callbacks.result_handler(status, handle->context);

        if (status == PAM_SUCCESS) {
            authenticating = false;
        }
    }

    pam_end(pam, 0);

    return NULL;
}

struct auth_handle_t* auth_begin_authentication(auth_callbacks_t callbacks, void *context)
{
    struct auth_handle_t *handle = malloc(sizeof(struct auth_handle_t));
    handle->callbacks = callbacks;
    handle->context = context;
    sem_init(&handle->prompt_semaphore, 0, 0);

    pthread_t auth_thread;
    if (pthread_create(&auth_thread, NULL, auth_thread_main, handle)) {
        fprintf(stderr, "Error creating auth thread\n");
    }

    return handle;
}


void auth_attempt_authentication(struct auth_handle_t *handle, auth_prompt_response_t response)
{
    memcpy(&handle->prompt_response, &response, sizeof(auth_prompt_response_t));
    sem_post(&handle->prompt_semaphore);
}


