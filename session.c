// session.c
// Implements session management logic.

#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

static Session sessions[MAX_SESSIONS];
static int session_count = 0;

/**
 * @brief Initializes the session management system.
 */
void init_sessions() {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        sessions[i].session_id[0] = '\0';
    }
}

/**
 * @brief Creates a new session for a given user.
 * @param username The username for the new session.
 * @return A pointer to the newly created session, or NULL if creation fails.
 */
Session* create_session(const char *username) {
    cleanup_sessions();
    if (session_count >= MAX_SESSIONS) {
        printf("Session limit reached.\n");
        return NULL;
    }
    
    int i = session_count; // Use the current count as the index
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    
    uuid_unparse_lower(binuuid, sessions[i].session_id);
    strncpy(sessions[i].username, username, sizeof(sessions[i].username) - 1);
    sessions[i].username[sizeof(sessions[i].username) - 1] = '\0';
    sessions[i].last_activity = time(NULL);
    
    session_count++;
    
    printf("Session created for user '%s' with ID '%s'\n", sessions[i].username, sessions[i].session_id);
    return &sessions[i];
}

/**
 * @brief Gets a session by its ID and updates its last activity time.
 * @param session_id The ID of the session to find.
 * @return A pointer to the found session, or NULL if not found.
 */
Session* get_session(const char *session_id) {
    cleanup_sessions();
    if (!session_id) {
        return NULL;
    }
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].session_id, session_id) == 0) {
            sessions[i].last_activity = time(NULL);
            return &sessions[i];
        }
    }
    return NULL;
}

/**
 * @brief Deletes a session by its session ID.
 * @param session_id The ID of the session to delete.
 */
void delete_session(const char *session_id) {
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].session_id, session_id) == 0) {
            // Shift elements to fill the gap
            sessions[i] = sessions[session_count - 1];
            session_count--;
            printf("Session with ID '%s' deleted.\n", session_id);
            break;
        }
    }
}

/**
 * @brief Checks if a session is valid (not expired).
 * @param session The session to check.
 * @return true if the session is valid, false otherwise.
 */
bool is_session_valid(Session *session) {
    return session != NULL;
}

/**
 * @brief Cleans up expired sessions.
 */
void cleanup_sessions() {
    time_t now = time(NULL);
    for (int i = 0; i < session_count; i++) {
        if (difftime(now, sessions[i].last_activity) > SESSION_TIMEOUT_SEC) {
            delete_session(sessions[i].session_id);
            i--; // Decrement i as the last element was moved to the current position
        }
    }
}


// // session.c
// // Implements session management logic.

// #include "session.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>
// #include <uuid/uuid.h>

// Session sessions[MAX_SESSIONS];
// int session_count = 0;

// // Function prototypes to resolve implicit declaration warnings
// void cleanup_sessions();

// /**
//  * @brief Creates a new session for a given user.
//  * @param username The username for the new session.
//  * @return A pointer to the newly created session, or NULL if creation fails.
//  */
// Session* create_session(const char *username) {
//     cleanup_sessions();
//     if (session_count >= MAX_SESSIONS) {
//         return NULL;
//     }
    
//     uuid_t binuuid;
//     uuid_generate_random(binuuid);
    
//     uuid_unparse_lower(binuuid, sessions[session_count].session_id);
//     strncpy(sessions[session_count].username, username, sizeof(sessions[session_count].username) - 1);
//     sessions[session_count].last_activity = time(NULL);
    
//     return &sessions[session_count++];
// }

// /**
//  * @brief Finds an existing session by its session ID.
//  * @param session_id The ID of the session to find.
//  * @return A pointer to the found session, or NULL if not found.
//  */
// Session* find_session(const char *session_id) {
//     cleanup_sessions();
//     for (int i = 0; i < session_count; i++) {
//         if (strcmp(sessions[i].session_id, session_id) == 0) {
//             sessions[i].last_activity = time(NULL);
//             return &sessions[i];
//         }
//     }
//     return NULL;
// }

// bool is_session_valid(Session* session){
// cleanup_sessions();
// for(int i = 0; i<session_count; i++){

// }
// }

// /**
//  * @brief Deletes a session by its session ID.
//  * @param session_id The ID of the session to delete.
//  */
// void delete_session(const char *session_id) {
//     for (int i = 0; i < session_count; i++) {
//         if (strcmp(sessions[i].session_id, session_id) == 0) {
//             sessions[i] = sessions[session_count - 1];
//             session_count--;
//             break;
//         }
//     }
// }

// /**
//  * @brief Cleans up expired sessions.
//  */
// void cleanup_sessions() {
//     time_t now = time(NULL);
//     for (int i = 0; i < session_count; i++) {
//         if (difftime(now, sessions[i].last_activity) > SESSION_TIMEOUT_SEC) {
//             delete_session(sessions[i].session_id);
//             i--; // Decrement i as the last element was moved to the current position
//         }
//     }
// }
