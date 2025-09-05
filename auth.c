// auth.c
// Implements user authentication and registration logic.

#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


/**
 * @brief Checks if a user's credentials are valid.
 * @param username The username to check.
 * @param password The password to check.
 * @return true if credentials are valid, false otherwise.
 */
bool authenticate_user(const char *username, const char *password) {
    FILE *file = fopen("users.txt", "r");
    if (!file) {
        printf("Error: 'users.txt' not found or could not be opened.\n");
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char stored_username[64];
        char stored_password[64];
        if (sscanf(line, "%63s %63s", stored_username, stored_password) == 2) {
            if (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0) {
                fclose(file);
                return true;
            }
        }
    }
    
    fclose(file);
    return false;
}

/**
 * @brief Registers a new user and stores their credentials.
 * @param username The new username.
 * @param password The new password.
 * @return true on successful registration, false if user already exists or on failure.
 */
bool register_user(const char *username, const char *password) {
    // Check if user already exists
    if (authenticate_user(username, password)) {
        printf("Registration failed: User '%s' already exists.\n", username);
        return false;
    }

    FILE *file = fopen("users.txt", "a"); // "a" for append mode
    if (!file) {
        perror("Error opening 'users.txt' for registration");
        return false;
    }

    fprintf(file, "%s %s\n", username, password);
    fclose(file);

    // Create the user's file directory immediately
    char user_path[256];
    snprintf(user_path, sizeof(user_path), "files/%s", username);
    if (mkdir(user_path, 0777) != 0) {
        perror("Failed to create user directory");
        // We can still proceed, but log the error
    }

    printf("User '%s' registered successfully.\n", username);
    return true;
}
