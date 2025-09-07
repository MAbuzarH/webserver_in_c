#include "http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>

#define FASTCGI_SOCK "/run/php/php8.3-fpm.sock"
#define FCGI_BEGIN_REQUEST 1
#define FCGI_END_REQUEST 3
#define FCGI_PARAMS 4
#define FCGI_STDIN 5
#define FCGI_STDOUT 6
#define FCGI_RESPONDER 1

// FastCGI header structure
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t padding_length;
    uint8_t reserved;
} FCGI_Header;

// Helper function to send a FastCGI packet
static int send_fcgi_packet(int sock, uint8_t type, uint16_t request_id, const char* content, uint16_t content_length) {
    FCGI_Header header;
    header.version = 1;
    header.type = type;
    header.request_id = htons(request_id);
    header.content_length = htons(content_length);
    header.padding_length = 0;
    header.reserved = 0;

    if (write(sock, &header, sizeof(header)) < 0) {
        perror("Failed to write FCGI header");
        return -1;
    }
    if (content_length > 0) {
        if (write(sock, content, content_length) < 0) {
            perror("Failed to write FCGI content");
            return -1;
        }
    }
    return 0;
}

// Helper to encode FastCGI name-value pairs, handling length encoding correctly.
static uint16_t encode_nv_pair(char* buf, const char* name, const char* value) {
    uint16_t len = 0;
    uint8_t name_len = strlen(name);
    uint8_t value_len = strlen(value);
    
    buf[len++] = name_len;
    buf[len++] = value_len;

    memcpy(buf + len, name, name_len);
    len += name_len;
    memcpy(buf + len, value, value_len);
    len += value_len;
    
    return len;
}

void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username) {
    int fastcgi_sock;
    struct sockaddr_un addr;
    uint16_t request_id = 1;

    fastcgi_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fastcgi_sock < 0) {
        perror("FastCGI socket creation failed");
        http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 23);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FASTCGI_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fastcgi_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect to PHP-FPM socket");
        close(fastcgi_sock);
        http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 23);
        return;
    }

    // Generate absolute paths
    char absolute_path[1024];
    char current_dir[512];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd() error");
        return;
    }
    
    // Correctly get the file path from the URL
    const char *url_path = req->url;
    if (url_path[0] == '/') {
        url_path++;
    }

    char file_path_only[512];
    const char *query_param = strchr(url_path, '?');
    if (query_param) {
        strncpy(file_path_only, url_path, query_param - url_path);
        file_path_only[query_param - url_path] = '\0';
    } else {
        strncpy(file_path_only, url_path, sizeof(file_path_only) - 1);
        file_path_only[sizeof(file_path_only) - 1] = '\0';
    }

   snprintf(absolute_path, sizeof(absolute_path), "%s/%s", current_dir, file_path_only);
    
    // Correctly set the DOCUMENT_ROOT to the base of user files
    char document_root[1024];
    snprintf(document_root, sizeof(document_root), "%s/user_files", current_dir);
    
    printf("absolute path in php:%s\n",absolute_path);
    fflush(stdout);
     printf("doc root in php:%s\n",document_root);
    fflush(stdout);

    // 1. Send FCGI_BEGIN_REQUEST packet
    uint8_t begin_req_body[8] = {0, FCGI_RESPONDER, 0, 0, 0, 0, 0, 0};
    if (send_fcgi_packet(fastcgi_sock, FCGI_BEGIN_REQUEST, request_id, (const char*)begin_req_body, 8) < 0) goto end;

    // 2. Build and send FCGI_PARAMS packets
    char params_buf[4096];
    uint16_t params_len = 0;
    
    params_len += encode_nv_pair(params_buf + params_len, "REQUEST_METHOD", req->method);
    params_len += encode_nv_pair(params_buf + params_len, "SCRIPT_FILENAME", absolute_path);
    params_len += encode_nv_pair(params_buf + params_len, "REQUEST_URI", req->url);
    params_len += encode_nv_pair(params_buf + params_len, "DOCUMENT_ROOT", document_root);
    params_len += encode_nv_pair(params_buf + params_len, "SERVER_SOFTWARE", "MyServer/1.0");
    params_len += encode_nv_pair(params_buf + params_len, "GATEWAY_INTERFACE", "FastCGI/1.0");
    params_len += encode_nv_pair(params_buf + params_len, "REMOTE_ADDR", "127.0.0.1");
    params_len += encode_nv_pair(params_buf + params_len, "SERVER_ADDR", "127.0.0.1");
    params_len += encode_nv_pair(params_buf + params_len, "SERVER_PORT", "8080");

    // Handle Query String
    if (query_param) {
        params_len += encode_nv_pair(params_buf + params_len, "QUERY_STRING", query_param + 1);
    } else {
        params_len += encode_nv_pair(params_buf + params_len, "QUERY_STRING", "");
    }
    
    if (send_fcgi_packet(fastcgi_sock, FCGI_PARAMS, request_id, params_buf, params_len) < 0) goto end;

    // 3. Send empty FCGI_PARAMS packet
    if (send_fcgi_packet(fastcgi_sock, FCGI_PARAMS, request_id, "", 0) < 0) goto end;

    // 4. Handle POST data
    if (strcmp(req->method, "POST") == 0) {
        const char *body_start = strstr(request_data, "\r\n\r\n") + 4;
        uint16_t body_len = strlen(body_start);
        
        if (send_fcgi_packet(fastcgi_sock, FCGI_STDIN, request_id, body_start, body_len) < 0) goto end;
    }
    
    // 5. Send empty FCGI_STDIN packet
    if (send_fcgi_packet(fastcgi_sock, FCGI_STDIN, request_id, "", 0) < 0) goto end;

    // 6. Read FCGI_STDOUT from PHP-FPM
    char response_buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fastcgi_sock, response_buffer, sizeof(response_buffer))) > 0) {
        write(client_socket, response_buffer, bytes_read);
    }
    
    if (bytes_read < 0) {
        perror("Failed to read from PHP-FPM socket");
    }

end:
    close(fastcgi_sock);
}