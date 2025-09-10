// #ifndef PHP_HANDLER_H
// #define PHP_HANDLER_H

// #include <stdbool.h>
// #include <unistd.h>
// #include <stdint.h>
// #include <sys/socket.h>

// // FastCGI constants
// #define FCGI_BEGIN_REQUEST       1
// #define FCGI_ABORT_REQUEST       2
// #define FCGI_END_REQUEST         3
// #define FCGI_PARAMS              4
// #define FCGI_STDIN               5
// #define FCGI_STDOUT              6
// #define FCGI_STDERR              7
// #define FCGI_DATA                8
// #define FCGI_GET_VALUES          9
// #define FCGI_GET_VALUES_RESULT  10
// #define FCGI_UNKNOWN_TYPE       11

// #define FCGI_RESPONDER           1
// #define FCGI_AUTHORIZER          2
// #define FCGI_FILTER              3

// // FastCGI header structure
// typedef struct {
//     uint8_t version;
//     uint8_t type;
//     uint16_t request_id;
//     uint16_t content_length;
//     uint8_t padding_length;
//     uint8_t reserved;
// } FCGI_Header;

// void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username);
// static uint16_t encode_nv_pair(char* buf, const char* name, const char* value);
// static int send_fcgi_packet(int sock, uint8_t type, uint16_t request_id, const char* content, uint16_t content_length);
// static int read_fcgi_response(int fastcgi_sock, int client_socket);
// //static void execute_php_via_cli(int client_socket, const char *php_file)
// void test_php_fpm_connection();
// #endif

#ifndef PHP_HANDLER_H
#define PHP_HANDLER_H

#include "http_handler.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>

// FastCGI constants (ensure these match your php_handler.h)
// // FastCGI constants
#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11

#define FCGI_RESPONDER           1
#define FCGI_AUTHORIZER          2
#define FCGI_FILTER              3

// FastCGI header structure
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t padding_length;
    uint8_t reserved;
} FCGI_Header;

// Forward declaration
// typedef struct httpreq httpreq;
void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username);
 static uint16_t encode_nv_pair(char* buf, const char* name, const char* value);
static int send_fcgi_packet(int sock, uint8_t type, uint16_t request_id, const char* content, uint16_t content_length);
 //static int read_fcgi_response(int fastcgi_sock, int client_socket);
static void execute_php_via_cli(int client_socket, const char *php_file);
static int test_php_fpm_connection();

void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username);

#endif