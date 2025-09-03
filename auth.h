// auth.h
// Defines structures and functions for user authentication.

#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>

/**
 * @brief Checks if a user exists in the user database.
 * @param username The username to check.
 * @return true if the user exists, false otherwise.
 */
bool user_exists(const char *username);

/**
 * @brief Registers a new user with a username and password.
 * @param username The new user's username.
 * @param password The new user's password.
 * @return true if registration is successful, false otherwise.
 */
bool register_user(const char *username, const char *password);

/**
 * @brief Authenticates a user based on their username and password.
 * @param username The username to authenticate.
 * @param password The password to authenticate.
 * @return true if credentials are valid, false otherwise.
 */
bool authenticate_user(const char *username, const char *password);

#endif
