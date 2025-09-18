// http_handler.h
// Defines structures and functions for handling HTTP requests and responses.

#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <stdbool.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define MAX_BOUNDARY_LENGTH 128
#define MAX_HEADER_LENGTH 1024
// #define BUFFER_SIZE 8192
// #define UPLOAD_DIR "uploads"

// typedef struct {
//     char *name;
//     char *filename;
//     char *content_type;
//     int is_file;
//     char *data;
//     size_t data_size;
// } form_data_t;

// typedef struct {
//     char boundary[MAX_BOUNDARY_LENGTH];
//     size_t boundary_len;
//     form_data_t *parts;
//     size_t part_count;
//     size_t part_capacity;
// } multipart_parser_t;

// // Function prototypes
// multipart_parser_t* multipart_parser_init(const char *content_type);
// void multipart_parser_free(multipart_parser_t *parser);
// int parse_multipart_data(multipart_parser_t *parser, const char *data, size_t data_len);
// int save_uploaded_files(multipart_parser_t *parser);
// const form_data_t* get_form_field(multipart_parser_t *parser, const char *field_name);

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
const char *find_header(const char *request, const char *header_name);

const char* find_boundary(const char *haystack, size_t haystack_len,
                          const char *needle, size_t needle_len);
size_t trim_trailing_crlf(const char *data_start,const char *data_end);          

//char* read_full_request(int client_socket, size_t *total_size_out);
char* read_full_request(int c);
httpreq *parse_http(const char *request);
void http_send_response(int client_socket, int status_code, const char *content_type, const char *body, size_t body_len);

void http_send_dashboard(int client_socket, const char *username, const char *path);

void http_send_login_page(int client_socket);
void http_send_register_page(int client_socket);
void http_send_welcome_page(int client_socket);
void http_send_redirect(int client_socket, const char *location);
void http_send_redirect_with_cookie(int client_socket, const char *location, const char *session_id);
const char *get_session_id_from_request(const char *request);
void urldecode(char *dest, const char *src);

bool http_handle_delete_file(const char *request, const char *username);
void http_handle_view_file(int client_socket, const char *url, const char *username);
void http_send_file_for_download(int client_socket, const char *url, const char *username);
int get_post_param(const char *body, const char *param_name, char *output, size_t output_size);
bool handle_create_folder(const char *request,const char *username);

int create_full_path(const char *path, mode_t mode);

void urlencode(char *dest, const char *src);

ssize_t recv_all(int sock, char *buf, size_t content_length);

void normalize_path(char *result, const char *base_path, const char *new_folder);

int http_handle_upload(const char *request, const char *username);
// bool http_handle_upload(int client_socket,const char *full_request, int content_length,const char *username);
//ssize_t parse_content_length_from_headers(const char *headers, size_t headers_len);
//ssize_t recv_headers(int sock, char *buf, size_t max) ;
bool http_handle_delete_folder(const char * request,const char *username);
#endif
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
// typedef struct {
//     const char *data;
//     size_t length;
//     char *filename;
//     char *name;
    
// } multipart_part_t;