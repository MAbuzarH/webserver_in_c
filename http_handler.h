// http_handler.h
// Header file for HTTP request and response handling functions.

#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <stdio.h>
#include <string.h>
#include <sys/socket.h> // For send and recv
#include <unistd.h>     // For close

#include <stddef.h>

typedef struct {
    char method[16];
    char url[256];
} httpreq;

typedef struct {
    char* fc;
    size_t size;
    char* filename;
} File;

// Function to parse an HTTP request
httpreq* parse_http(const char* request);

// Functions to send different types of HTTP responses
void http_send_response(int c, int status_code, const char* content_type, const char* body, size_t body_len);
void http_send_redirect(int c, const char* url);
void http_send_redirect_with_cookie(int c, const char* url, const char* session_id);
void http_send_login_page(int c);
void http_send_dashboard(int c, const char* username);

// Functions for file handling
int http_handle_upload(const char* request, const char* username);
int http_handle_delete_file(const char* request, const char* username);
File* fileread(const char* filename);
void free_file(File* f);

// Utility functions
const char* get_status_message(int status_code);
const char* get_content_type(const char* filename);
void urldecode(char *dst, const char *src);
char* read_full_request(int c);
const char* get_session_id_from_request(const char* request);


// // Function to parse an HTTP request
// httpreq* parse_http(const char* request);

// // Functions to send different types of HTTP responses
// void http_send_response(int c, int status_code, const char* content_type, const char* body, size_t body_len);
// void http_send_redirect(int c, const char* url);
// void http_send_redirect_with_cookie(int c, const char* url, const char* session_id);
// void http_send_login_page(int c);
// void http_send_dashboard(int c, const char* username);

// // Functions for file handling
// int http_handle_upload(const char* request, const char* username);
// int http_handle_delete_file(const char* request, const char* username);
// File* fileread(const char* filename);
// void free_file(File* f);

// // Utility functions
// const char* get_status_message(int status_code);
// const char* get_content_type(const char* filename);
// void urldecode(char *dst, const char *src);
// char* read_full_request(int c);
// const char* get_session_id_from_request(const char* request);

// // Struct to represent a parsed HTTP request.
// struct sHttpreq {
//     char method[8];
//     char url[128];
// };
// typedef struct sHttpreq httpreq;


// // Struct to hold file contents.
// struct sFile {
//     char *filename;
//     char *fc; // file content (dynamically allocated)
//     int size;
// };
// typedef struct sFile File;

// // Function prototypes
// char* read_full_request(int c);
// httpreq* parse_http(const char* request);
// void http_send_response(int c, int status_code, const char* content_type, const char* body, size_t body_len);
// void http_send_redirect(int c, const char* url);
// void http_send_redirect_with_cookie(int c, const char* url, const char* session_id);
// const char* get_status_message(int status_code);
// const char* get_content_type(const char* filename);
// void urldecode(char *dst, const char *src);
// File* fileread(const char* filename);
// int http_handle_upload(const char *request, const char* username);
// void http_send_dashboard(int c, const char* username);

#endif // HTTP_HANDLER_H
