#ifndef PHP_HANDLER_H
#define PHP_HANDLER_H
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
// FastCGI header structure
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t padding_length;
    uint8_t reserved;
} FCGI_Header;

void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username);
static uint16_t encode_nv_pair(char* buf, const char* name, const char* value);
static int send_fcgi_packet(int sock, uint8_t type, uint16_t request_id, const char* content, uint16_t content_length) ;
#endif