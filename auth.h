// auth.h
// Header file for user authentication functions.

#ifndef AUTH_H
#define AUTH_H

#include<stdbool.h>

#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 64
#define HASH_LEN 65 // SHA-256 hash is 64 hex characters + null terminator

// A struct to hold the parsed form data for user login/registration.
struct FormData {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
     char message[512];
};
typedef struct {
    char username[256];
    char password[256];
} LoginData;
// // Function prototypes
void hash_password(const char* password, char* output);
//struct FormData parse_user_data(char *body_data);
int user_exists(const char* username);
//int register_user(const char* username, const char* password);
//int authenticate_user(const char* username, const char* password);

// // Session management functions
 char* create_session(const char* username);
 char* get_username_from_session(const char* session_id);

/**
 * Parses form data from a request body.
 * @param body The body string from a POST request.
 * @return A FormData struct with the username and password.
 */
struct FormData parse_user_data(char* body);

/**
 * Registers a new user.
 * @param username The username to register.
 * @param password The password for the user.
 * @return true if registration is successful, false otherwise.
 */
bool register_user(const char* username, const char* password);

/**
 * Authenticates a user based on their username and password.
 * @param username The username to authenticate.
 * @param password The password to check.
 * @return true if authentication is successful, false otherwise.
 */
bool authenticate_user(const char* username, const char* password);

/**
 * Creates a new session for a user and stores it in a file.
 * @param username The username of the logged-in user.
 * @return The session ID string, or NULL on error.
 */
char* create_session(const char* username);

/**
 * Retrieves the username associated with a given session ID from a file.
 * @param session_id The session ID to look up.
 * @return The username string, or NULL if the session is not found or expired.
 */
char* get_username_from_session(const char* session_id);

/**
 * Deletes a session file, effectively logging the user out.
 * @param session_id The session ID to delete.
 */
void delete_session(const char* session_id);

// Struct to hold parsed login data


// Function to parse login credentials from a POST request body
int parse_login_data(const char* request_body, LoginData* data);

// Function to authenticate a user
bool authenticate_user(const char* username, const char* password);




#endif // AUTH_H
