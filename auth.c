// auth.c
// Implements user registration, authentication, and session management.

#include "auth.h"
#include "http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>   // for timeval struct
#include <sys/stat.h>   // For stat()


// #define SESSIONS_DIR "sessions"
// #define USERS_FILE "users.txt"

// auth.c
// Implements user authentication logic.

#include "auth.h"
#include <string.h>
#include <stdio.h>

/**
 * Parses a simple POST request body for username and password.
 */
int parse_login_data(const char* request_body, LoginData* data) {
    if (!request_body || !data) {
        return 0;
    }
    
    // Scan for username and password. Assumes a simple key=value&key=value format.
    if (sscanf(request_body, "username=%255[^&]&password=%255s", data->username, data->password) == 2) {
        return 1;
    }

    return 0;
}

/**
 * Checks if the provided username and password are valid.
 * This is a hardcoded example for demonstration.
 */
bool authenticate_user(const char* username, const char* password) {
    if (!username || !password) {
        return false;
    }
    // Simple authentication check with a single hardcoded user
    return (strcmp(username, "test_user") == 0 && strcmp(password, "test_pass") == 0);
}


// // Creates the sessions directory if it doesn't exist.
// void create_sessions_directory() {
//     struct stat st = {0};
//     if (stat(SESSIONS_DIR, &st) == -1) {
//         mkdir(SESSIONS_DIR, 0700);
//     }
// }

// // A simple, non-cryptographic password hashing function.
// void hash_password(const char* password, char* output) {
//     // For now, a simple, non-secure hash for demonstration.
//     // In a real application, you would use a secure library like bcrypt or Argon2.
//     strncpy(output, password, HASH_LEN - 1);
//     output[HASH_LEN - 1] = '\0';
// }


// // Parses form data from a request body.
// struct FormData parse_user_data(char* body) {
//    struct FormData data;
//     memset(&data, 0, sizeof(data));

//     char* username_start = strstr(body, "username=");
//     char* password_start = strstr(body, "password=");

//     if (username_start) {
//         username_start += strlen("username=");
//         char* username_end = strchr(username_start, '&');
//         if (username_end) {
//             strncpy(data.username, username_start, username_end - username_start);
//             data.username[username_end - username_start] = '\0';
//         } else {
//             strncpy(data.username, username_start, sizeof(data.username) - 1);
//             data.username[sizeof(data.username) - 1] = '\0';
//         }
//     }

//     if (password_start) {
//         password_start += strlen("password=");
//         strncpy(data.password, password_start, sizeof(data.password) - 1);
//         data.password[sizeof(data.password) - 1] = '\0';
//     }

//     return data;
// }

// // Registers a new user by appending their credentials to a file.
// bool register_user(const char* username, const char* password) {
//     FILE* fp = fopen(USERS_FILE, "r");
//     if (fp) {
//         char line[512];
//         while (fgets(line, sizeof(line), fp)) {
//             char existing_username[256];
//             sscanf(line, "%s", existing_username);
//             if (strcmp(existing_username, username) == 0) {
//                 fclose(fp);
//                 return false; // User already exists
//             }
//         }
//         fclose(fp);
//     }

//     fp = fopen(USERS_FILE, "a");
//     if (!fp) {
//         return false;
//     }

//     fprintf(fp, "%s %s\n", username, password);
//     fclose(fp);
//     return true;
// }

// // Authenticates a user against the user file.
// bool authenticate_user(const char* username, const char* password) {
//     FILE* fp = fopen(USERS_FILE, "r");
//     if (!fp) {
//         return false;
//     }

//     char file_username[256];
//     char file_password[256];
//     while (fscanf(fp, "%s %s", file_username, file_password) != EOF) {
//         if (strcmp(file_username, username) == 0 && strcmp(file_password, password) == 0) {
//             fclose(fp);
//             return true;
//         }
//     }
//     fclose(fp);
//     return false;
// }

// // Creates a new session by saving the username to a file.
// char* create_session(const char* username) {
//     create_sessions_directory();
//     long long timestamp = (long long)time(NULL);
//     char* session_id = (char*)malloc(33);
//     if (!session_id) {
//         return NULL;
//     }
//     snprintf(session_id, 33, "%lld", timestamp);

//     char session_file_path[512];
//     snprintf(session_file_path, sizeof(session_file_path), "%s/%s.session", SESSIONS_DIR, session_id);

//     FILE* fp = fopen(session_file_path, "w");
//     if (!fp) {
//         free(session_id);
//         return NULL;
//     }

//     fprintf(fp, "%s", username);
//     fclose(fp);

//     return session_id;
// }

// // Retrieves the username from a session file.
// char* get_username_from_session(const char* session_id) {
//     if (!session_id || strlen(session_id) == 0) {
//         return NULL;
//     }

//     char session_file_path[512];
//     snprintf(session_file_path, sizeof(session_file_path), "%s/%s.session", SESSIONS_DIR, session_id);
    
//     FILE* fp = fopen(session_file_path, "r");
//     if (!fp) {
//         return NULL;
//     }

//     char* username = (char*)malloc(MAX_USERNAME_LEN + 1);
//     if (!username) {
//         fclose(fp);
//         return NULL;
//     }

//     if (fgets(username, MAX_USERNAME_LEN, fp) == NULL) {
//         free(username);
//         username = NULL;
//     }
    
//     fclose(fp);

//     // Remove any trailing newline characters
//     if (username) {
//         username[strcspn(username, "\r\n")] = 0;
//     }

//     return username;
// }

// // Deletes a session file.
// void delete_session(const char* session_id) {
//     if (!session_id || strlen(session_id) == 0) {
//         return;
//     }

//     char session_file_path[512];
//     snprintf(session_file_path, sizeof(session_file_path), "%s/%s.session", SESSIONS_DIR, session_id);
//     remove(session_file_path);
// }

// /**
//  * Parses URL-encoded form data, specifically for username and password.
//  * @param body_data The raw URL-encoded string.
//  * @return A new FormData struct with parsed data.
//  */
// // struct FormData parse_user_data(char *body_data) {
// //     struct FormData data;
//     memset(&data, 0, sizeof(data));
    
//     char *username_start = strstr(body_data, "username=");
//     char *password_start = strstr(body_data, "password=");

//     if (username_start) {
//         username_start += strlen("username=");
//         char *username_end = strchr(username_start, '&');
//         if (username_end) {
//             size_t username_len = username_end - username_start;
//             strncpy(data.username, username_start, username_len > sizeof(data.username) - 1 ? sizeof(data.username) - 1 : username_len);
//             data.username[username_len > sizeof(data.username) - 1 ? sizeof(data.username) - 1 : username_len] = '\0';
//         } else {
//             strncpy(data.username, username_start, sizeof(data.username) - 1);
//             data.username[sizeof(data.username) - 1] = '\0';
//         }
//     }

//     if (password_start) {
//         password_start += strlen("password=");
//         size_t password_len = strlen(password_start);
//         strncpy(data.password, password_start, password_len > sizeof(data.password) - 1 ? sizeof(data.password) - 1 : password_len);
//         data.password[password_len > sizeof(data.password) - 1 ? sizeof(data.password) - 1 : password_len] = '\0';
//     }
    
//     urldecode(data.username, data.username);
//     urldecode(data.password, data.password);
    
//     return data;
// }

// /**
//  * Checks if a username already exists in the users.txt file.
//  * @param username The username to check.
//  * @return 1 if the user exists, 0 otherwise.
//  */
// int user_exists(const char* username) {
//     FILE* fp = fopen("users.txt", "r");
//     if (!fp) {
//         return 0;
//     }
//     char line[512];
//     while (fgets(line, sizeof(line), fp)) {
//         char stored_username[MAX_USERNAME_LEN];
//         char* colon = strchr(line, ':');
//         if (colon) {
//             size_t len = colon - line;
//             if (len < MAX_USERNAME_LEN) {
//                 strncpy(stored_username, line, len);
//                 stored_username[len] = '\0';
//                 if (strcmp(stored_username, username) == 0) {
//                     fclose(fp);
//                     return 1;
//                 }
//             }
//         }
//     }
//     fclose(fp);
//     return 0;
// }

// /**
//  * Registers a new user.
//  * @param username The new user's username.
//  * @param password The new user's password.
//  * @return 1 on success, 0 on failure.
//  */
// int register_user(const char* username, const char* password) {
//     if (user_exists(username)) {
//         return 0;
//     }

//     FILE* fp = fopen("users.txt", "a");
//     if (!fp) {
//         perror("Error opening users.txt for writing");
//         return 0;
//     }
    
//     char hashed_password[HASH_LEN];
//     hash_password(password, hashed_password);
    
//     fprintf(fp, "%s:%s\n", username, hashed_password);
//     fclose(fp);
//     return 1;
// }

// /**
//  * Authenticates a user.
//  * @param username The username to authenticate.
//  * @param password The password to authenticate.
//  * @return 1 on successful authentication, 0 otherwise.
//  */
// int authenticate_user(const char* username, const char* password) {
//     FILE* fp = fopen("users.txt", "r");
//     if (!fp) {
//         return 0;
//     }
    
//     char line[512];
//     char hashed_password[HASH_LEN];
//     hash_password(password, hashed_password);
    
//     while (fgets(line, sizeof(line), fp)) {
//         char stored_username[MAX_USERNAME_LEN];
//         char stored_hash[HASH_LEN];
        
//         char* colon = strchr(line, ':');
//         if (colon) {
//             size_t len = colon - line;
//             if (len < MAX_USERNAME_LEN) {
//                 strncpy(stored_username, line, len);
//                 stored_username[len] = '\0';
                
//                 char* newline = strchr(colon + 1, '\n');
//                 if (newline) *newline = '\0';
//                 strncpy(stored_hash, colon + 1, HASH_LEN - 1);
                
//                 if (strcmp(stored_username, username) == 0 && strcmp(stored_hash, hashed_password) == 0) {
//                     fclose(fp);
//                     return 1;
//                 }
//             }
//         }
//     }
//     fclose(fp);
//     return 0;
// }

// // Global sessions list for a multi-process server.
// // In a multi-threaded server, this would need a mutex.
// struct Session {
//     char session_id[33]; // UUID-like string
//     char username[MAX_USERNAME_LEN];
//     time_t last_activity;
// };

// // Global array of sessions. For a real server, this would be a dynamic data structure.
// #define MAX_SESSIONS 100
// struct Session sessions[MAX_SESSIONS];
// int session_count = 0;

// /**
//  * Creates a unique session ID for a user.
//  * @param username The username for the new session.
//  * @return A new session ID string, or NULL on failure.
//  */
// char* create_session(const char* username) {
//     if (session_count >= MAX_SESSIONS) {
//         return NULL; // No space for new sessions
//     }

//     // Generate a simple, time-based session ID.
//     time_t now = time(NULL);
//     snprintf(sessions[session_count].session_id, sizeof(sessions[session_count].session_id),
//              "%lu%d", (unsigned long)now, getpid()); // Not a truly secure UUID but works for demo

//     strncpy(sessions[session_count].username, username, sizeof(sessions[session_count].username) - 1);
//     sessions[session_count].username[sizeof(sessions[session_count].username) - 1] = '\0';
//     sessions[session_count].last_activity = now;

//     char* new_id = strdup(sessions[session_count].session_id);
//     session_count++;
//     return new_id;
// }

// /**
//  * Retrieves the username associated with a session ID.
//  * @param session_id The session ID to look up.
//  * @return The username string, or NULL if the session is not found or expired.
//  */
// char* get_username_from_session(const char* session_id) {
//     time_t now = time(NULL);
//     for (int i = 0; i < session_count; i++) {
//         // Check for session ID match and if it's still active (e.g., within 30 minutes).
//         if (strcmp(sessions[i].session_id, session_id) == 0 && (now - sessions[i].last_activity) < 1800) {
//             sessions[i].last_activity = now; // Update activity time
//             return sessions[i].username;
//         }
//     }
//     return NULL;
// }



// // // auth.c
// // // Implements user registration and authentication logic.

// // #include "auth.h"
// // #include "http_handler.h" // Needed for urldecode() in parse_user_data()
// // #include <stdio.h>
// // #include <stdlib.h>
// // #include <time.h>       // For getting the current time
// // #include <sys/time.h>   // for timeval struct
// // #include <string.h>

// /**
//  * A basic, non-cryptographic password hashing function for demonstration purposes.
//  * @param password The password string to hash.
//  * @param output The buffer to store the hash.
//  */
// void hash_password(const char* password, char* output) {
//     // For now, we'll just copy the password (for illustration) or a simple hash.
//     strncpy(output, password, HASH_LEN - 1);
//     output[HASH_LEN - 1] = '\0';
// }







// /**
//  * Parses URL-encoded form data, specifically for username and password.
//  * @param body_data The raw URL-encoded string.
//  * @return A new FormData struct with parsed data.
//  */
// struct FormData parse_user_data(char *body_data) {
//     struct FormData data;
//     memset(&data, 0, sizeof(data));
    
//     char *username_start = strstr(body_data, "username=");
//     char *password_start = strstr(body_data, "password=");

//     if (username_start) {
//         username_start += strlen("username=");
//         char *username_end = strchr(username_start, '&');
//         if (username_end) {
//             size_t username_len = username_end - username_start;
//             strncpy(data.username, username_start, username_len > sizeof(data.username) - 1 ? sizeof(data.username) - 1 : username_len);
//             data.username[username_len > sizeof(data.username) - 1 ? sizeof(data.username) - 1 : username_len] = '\0';
//         } else {
//             strncpy(data.username, username_start, sizeof(data.username) - 1);
//             data.username[sizeof(data.username) - 1] = '\0';
//         }
//     }

//     if (password_start) {
//         password_start += strlen("password=");
//         size_t password_len = strlen(password_start);
//         strncpy(data.password, password_start, password_len > sizeof(data.password) - 1 ? sizeof(data.password) - 1 : password_len);
//         data.password[password_len > sizeof(data.password) - 1 ? sizeof(data.password) - 1 : password_len] = '\0';
//     }
    
//     urldecode(data.username, data.username);
//     urldecode(data.password, data.password);
    
//     return data;
// }

// /**
//  * Checks if a username already exists in the users.txt file.
//  * @param username The username to check.
//  * @return 1 if the user exists, 0 otherwise.
//  */
// int user_exists(const char* username) {
//     FILE* fp = fopen("users.txt", "r");
//     if (!fp) {
//         return 0;
//     }
//     char line[512];
//     while (fgets(line, sizeof(line), fp)) {
//         char stored_username[MAX_USERNAME_LEN];
//         char* colon = strchr(line, ':');
//         if (colon) {
//             size_t len = colon - line;
//             if (len < MAX_USERNAME_LEN) {
//                 strncpy(stored_username, line, len);
//                 stored_username[len] = '\0';
//                 if (strcmp(stored_username, username) == 0) {
//                     fclose(fp);
//                     return 1;
//                 }
//             }
//         }
//     }
//     fclose(fp);
//     return 0;
// }

// /**
//  * Registers a new user.
//  * @param username The new user's username.
//  * @param password The new user's password.
//  * @return 1 on success, 0 on failure.
//  */
// int register_user(const char* username, const char* password) {
//     if (user_exists(username)) {
//         return 0;
//     }

//     FILE* fp = fopen("users.txt", "a");
//     if (!fp) {
//         perror("Error opening users.txt for writing");
//         return 0;
//     }
    
//     char hashed_password[HASH_LEN];
//     hash_password(password, hashed_password);
    
//     fprintf(fp, "%s:%s\n", username, hashed_password);
//     fclose(fp);
//     return 1;
// }

// /**
//  * Authenticates a user.
//  * @param username The username to authenticate.
//  * @param password The password to authenticate.
//  * @return 1 on successful authentication, 0 otherwise.
//  */
// int authenticate_user(const char* username, const char* password) {
//     FILE* fp = fopen("users.txt", "r");
//     if (!fp) {
//         return 0;
//     }
    
//     char line[512];
//     char hashed_password[HASH_LEN];
//     hash_password(password, hashed_password);
    
//     while (fgets(line, sizeof(line), fp)) {
//         char stored_username[MAX_USERNAME_LEN];
//         char stored_hash[HASH_LEN];
        
//         char* colon = strchr(line, ':');
//         if (colon) {
//             size_t len = colon - line;
//             if (len < MAX_USERNAME_LEN) {
//                 strncpy(stored_username, line, len);
//                 stored_username[len] = '\0';
                
//                 char* newline = strchr(colon + 1, '\n');
//                 if (newline) *newline = '\0';
//                 strncpy(stored_hash, colon + 1, HASH_LEN - 1);
                
//                 if (strcmp(stored_username, username) == 0 && strcmp(stored_hash, hashed_password) == 0) {
//                     fclose(fp);
//                     return 1;
//                 }
//             }
//         }
//     }
//     fclose(fp);
//     return 0;
// }

// // Global sessions list for a multi-process server.
// // In a multi-threaded server, this would need a mutex.
// struct Session {
//     char session_id[33]; // UUID-like string
//     char username[MAX_USERNAME_LEN];
//     time_t last_activity;
// };

// // Global array of sessions. For a real server, this would be a dynamic data structure.
// #define MAX_SESSIONS 100
// struct Session sessions[MAX_SESSIONS];
// int session_count = 0;

// /**
//  * Creates a unique session ID for a user.
//  * @param username The username for the new session.
//  * @return A new session ID string, or NULL on failure.
//  */
// char* create_session(const char* username) {
//     if (session_count >= MAX_SESSIONS) {
//         return NULL; // No space for new sessions
//     }

//     // Generate a simple, time-based session ID.
//     time_t now = time(NULL);
//     snprintf(sessions[session_count].session_id, sizeof(sessions[session_count].session_id),
//              "%lu%d", (unsigned long)now, getpid()); // Not a truly secure UUID but works for demo

//     strncpy(sessions[session_count].username, username, sizeof(sessions[session_count].username) - 1);
//     sessions[session_count].username[sizeof(sessions[session_count].username) - 1] = '\0';
//     sessions[session_count].last_activity = now;

//     char* new_id = strdup(sessions[session_count].session_id);
//     session_count++;
//     return new_id;
// }

// /**
//  * Retrieves the username associated with a session ID.
//  * @param session_id The session ID to look up.
//  * @return The username string, or NULL if the session is not found or expired.
//  */
// char* get_username_from_session(const char* session_id) {
//     time_t now = time(NULL);
//     for (int i = 0; i < session_count; i++) {
//         // Check for session ID match and if it's still active (e.g., within 30 minutes).
//         if (strcmp(sessions[i].session_id, session_id) == 0 && (now - sessions[i].last_activity) < 1800) {
//             sessions[i].last_activity = now; // Update activity time
//             return sessions[i].username;
//         }
//     }
//     return NULL;
// }




// /**
//  * A basic, non-cryptographic password hashing function for demonstration purposes.
//  * In a real-world application, you would use a dedicated library like OpenSSL
//  * with a strong hashing algorithm and salt.
//  * This function just takes the first 64 characters of the password and converts it
//  * to a simple representation to illustrate the concept.
//  *
//  * @param password The password string to hash.
//  * @param output The buffer to store the hash. Must be at least HASH_LEN long.
//  */
// void hash_password(const char* password, char* output) {
//     // This is a placeholder. A real implementation would use a library like OpenSSL.
//     // For now, we'll just copy the password (for illustration) or a simple hash.
//     strncpy(output, password, HASH_LEN - 1);
//     output[HASH_LEN - 1] = '\0';
// }

