// session.h
// Defines structures and functions for managing user sessions.

#ifndef SESSION_H
#define SESSION_H

#include <time.h>
#include <stdbool.h>

#define MAX_SESSIONS 100
#define SESSION_TIMEOUT_SEC 3600
#define MAX_USERNAME_LENGTH 63
// Represents a user session with a unique ID and associated username.
typedef struct {
    char session_id[37]; // UUID
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
} Session;

// An array to store all active sessions.
// extern Session sessions[MAX_SESSIONS];
// extern int session_count =0;

/**
 * @brief Cleans up expired sessions.
 */
void cleanup_sessions();

/**
 * @brief Creates a new session for a given user.
 * @param username The username for the new session.
 * @return A pointer to the newly created session, or NULL if creation fails.
 */
Session* create_session(const char *username);

/**
 * @brief Finds an existing session by its session ID.
 * @param session_id The ID of the session to find.
 * @return A pointer to the found session, or NULL if not found.
 */
Session* find_session(const char *session_id);

/**
 * @brief Deletes a session by its session ID.
 * @param session_id The ID of the session to delete.
 */
void delete_session(const char *session_id);

void init_sessions();
// Session *create_session(const char *username);
// void destroy_session(const char *session_id);
Session *get_session(const char *session_id); // <<< Missing declaration added
bool is_session_valid(Session *session); // <<< Missing declaration added
// void cleanup_sessions();

#endif
