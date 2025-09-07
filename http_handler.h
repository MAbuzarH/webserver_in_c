// http_handler.h
// Defines structures and functions for handling HTTP requests and responses.

#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>

typedef struct {
    char *method;
    char *url;
    char *protocol;
    char *body;
} httpreq;

typedef struct {
    char *fc;
    int size;
} File;

bool has_file_extension(const char *url, const char *ext);

// /**
//  * @brief Reads the full HTTP request from the client socket.
//  * @param c The client socket file descriptor.
//  * @return A dynamically allocated string containing the full request, or NULL on error.
//  */
// char *read_full_request(int c);

// /**
//  * @brief Parses an HTTP request string into a structured httpreq object.
//  * @param req_str The request string to parse.
//  * @return A pointer to the parsed httpreq struct, or NULL on parsing failure.
//  */
// httpreq *parse_http(const char *req_str);

// /**
//  * @brief Sends an HTTP response to the client.
//  * @param c The client socket file descriptor.
//  * @param status_code The HTTP status code (e.g., 200, 404).
//  * @param content_type The MIME type of the content (e.g., "text/html").
//  * @param content The content to send in the response body.
//  * @param content_length The length of the content.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *content, size_t content_length);

// /**
//  * @brief Sends an HTTP redirect response to a new URL.
//  * @param c The client socket file descriptor.
//  * @param url The URL to redirect to.
//  */
// void http_send_redirect(int c, const char *url);

// /**
//  * @brief Sends an HTTP redirect response with a session cookie.
//  * @param c The client socket file descriptor.
//  * @param url The URL to redirect to.
//  * @param session_id The session ID to set in the cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id);

// /**
//  * @brief Handles the login page and form submission.
//  * @param c The client socket file descriptor.
//  */
// void http_send_login_page(int c);

// /**
//  * @brief Handles the file upload logic for an authenticated user.
//  * @param request The full HTTP request string.
//  * @param username The username of the authenticated user.
//  * @return true if upload is successful, false otherwise.
//  */
// bool http_handle_upload(const char *request, const char *username);

// /**
//  * @brief Handles the file deletion logic for an authenticated user.
//  * @param request The full HTTP request string.
//  * @param username The username of the authenticated user.
//  * @return true if deletion is successful, false otherwise.
//  */
// bool http_handle_delete_file(const char *request, const char *username);

// /**
//  * @brief Sends the dashboard HTML page to the client.
//  * @param c The client socket file descriptor.
//  * @param username The username of the authenticated user.
//  */
// void http_send_dashboard(int c, const char *username);

// /**
//  * @brief Reads the content of a file into a dynamically allocated buffer.
//  * @param path The path to the file.
//  * @return A pointer to a File struct containing the file content and size, or NULL on failure.
//  */
// File *fileread(const char *path);

// /**
//  * @brief Frees the memory allocated for a File struct.
//  * @param f The File struct to free.
//  */
// void free_file(File *f);

// /**
//  * @brief Gets the MIME type of a file based on its extension.
//  * @param filename The name of the file.
//  * @return A string representing the MIME type.
//  */
// const char *get_content_type(const char *filename);

// /**
//  * @brief Gets the session ID from an HTTP request's Cookie header.
//  * @param request The full HTTP request string.
//  * @return The session ID string, or NULL if not found.
//  */
// const char *get_session_id_from_request(const char *request);


// // New function to send the welcome page
// void http_send_welcome_page(int client_socket);

// // New helper function for robust POST body parsing
// int get_post_param(const char *body, const char *param_name, char *output, size_t output_size);
// // New function to send the welcome page
// void http_send_welcome_page(int client_socket);

// // New helper function for robust POST body parsing
// int get_post_param(const char *body, const char *param_name, char *output, size_t output_size);

// void http_handle_view_file(int c, const char *url, const char *username);

// /**
//  * @brief Decodes URL-encoded strings.
//  * @param dest The destination buffer for the decoded string.
//  * @param src The source URL-encoded string.
//  */
// void urldecode(char *dest, const char *src);
// http_handler.h
// Defines HTTP request parsing and response sending functions.

// Function prototypes
char *read_full_request(int client_socket);
httpreq *parse_http(const char *request);
void http_send_response(int client_socket, int status_code, const char *content_type, const char *body, size_t body_len);
//void http_send_dashboard(int client_socket, const char *username);
void http_send_dashboard(int client_socket, const char *username, const char *path);
void http_send_login_page(int client_socket);
void http_send_register_page(int client_socket);
void http_send_welcome_page(int client_socket);
void http_send_redirect(int client_socket, const char *location);
void http_send_redirect_with_cookie(int client_socket, const char *location, const char *session_id);
const char *get_session_id_from_request(const char *request);
void urldecode(char *dest, const char *src);
//bool http_handle_upload(const char *request, const char *username);
bool http_handle_delete_file(const char *request, const char *username);
void http_handle_view_file(int client_socket, const char *url, const char *username);
void http_send_file_for_download(int client_socket, const char *url, const char *username);
int get_post_param(const char *body, const char *param_name, char *output, size_t output_size);
bool handle_create_folder(const char *request,const char *username);
bool http_handle_upload(int client_socket,const char *request, const char *username);
int create_full_path(const char *path, mode_t mode);
void normalize_path(char *result, const char *base_path, const char *new_folder);
bool http_handle_delete_folder(const char * request,const char *username);
#endif
