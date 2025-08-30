// session.c
// Implements session creation and management.

#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

#define MAX_SESSIONS 100
#define SESSION_TIMEOUT_SEC 3600 // 1 hour

static Session sessions[MAX_SESSIONS];
static int session_count = 0;

/**
 * Creates a new session for a user.
 */
char* create_session(const char* username) {
    if (session_count >= MAX_SESSIONS) {
        return NULL;
    }
    
    // Generate a unique session ID
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    
    // Find an empty slot
    int i;
    for (i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].session_id[0] == '\0') {
            break;
        }
    }
    
    if (i == MAX_SESSIONS) {
        return NULL; // No free slots
    }

    // Initialize the new session
    strncpy(sessions[i].session_id, uuid_str, sizeof(sessions[i].session_id) - 1);
    sessions[i].session_id[32] = '\0';
    strncpy(sessions[i].username, username, sizeof(sessions[i].username) - 1);
    sessions[i].username[sizeof(sessions[i].username) - 1] = '\0';
    sessions[i].expiration_time = time(NULL) + SESSION_TIMEOUT_SEC;
    session_count++;

    return sessions[i].session_id;
}

/**
 * Finds a session by its ID.
 */
Session* find_session(const char* session_id) {
    if (!session_id) {
        return NULL;
    }
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].session_id, session_id) == 0) {
            if (time(NULL) < sessions[i].expiration_time) {
                return &sessions[i];
            } else {
                // Session expired, delete it
                delete_session(session_id);
                return NULL;
            }
        }
    }
    return NULL;
}

/**
 * Deletes a session by its ID.
 */
void delete_session(const char* session_id) {
    if (!session_id) {
        return;
    }
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].session_id, session_id) == 0) {
            memset(&sessions[i], 0, sizeof(Session));
            session_count--;
            break;
        }
    }
}
