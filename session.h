// session.h
// Provides function declarations for session management.

#ifndef SESSION_H
#define SESSION_H

#include <time.h>

// The Session struct definition
typedef struct {
    char session_id[33]; // 32 chars for the UUID + 1 for null terminator
    char username[256];
    time_t expiration_time;
} Session;

// Function to create a new session
char* create_session(const char* username);

// Function to find an existing session by ID
Session* find_session(const char* session_id);

// Function to delete a session
void delete_session(const char* session_id);

#endif // SESSION_H