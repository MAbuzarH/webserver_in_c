#include "http_handler.h"
#include "php_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>

#define FASTCGI_SOCK "/run/php/php8.3-fpm.sock"



// Helper function to send a FastCGI packet
static int send_fcgi_packet(int sock, uint8_t type, uint16_t request_id, 
                           const char* content, uint16_t content_length) {
    FCGI_Header header;
    header.version = 1;
    header.type = type;
    header.request_id = htons(request_id);
    header.content_length = htons(content_length);
    header.padding_length = 0;
    header.reserved = 0;

    // Send header
    if (write(sock, &header, sizeof(header)) != sizeof(header)) {
        perror("Failed to write FCGI header");
        return -1;
    }

    // Send content if any
    if (content_length > 0) {
        if (write(sock, content, content_length) != content_length) {
            perror("Failed to write FCGI content");
            return -1;
        }
    }

    return 0;
}

// Helper to encode FastCGI name-value pairs
static uint16_t encode_nv_pair(char* buf, const char* name, const char* value) {
    uint16_t len = 0;
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    
    // Handle length encoding (simplified version)
    if (name_len < 128) {
        buf[len++] = name_len;
    } else {
        // For longer names, we'd need proper encoding, but this should work for most cases
        buf[len++] = (name_len >> 24) | 0x80;
        buf[len++] = (name_len >> 16) & 0xFF;
        buf[len++] = (name_len >> 8) & 0xFF;
        buf[len++] = name_len & 0xFF;
    }
    
    if (value_len < 128) {
        buf[len++] = value_len;
    } else {
        buf[len++] = (value_len >> 24) | 0x80;
        buf[len++] = (value_len >> 16) & 0xFF;
        buf[len++] = (value_len >> 8) & 0xFF;
        buf[len++] = value_len & 0xFF;
    }

    memcpy(buf + len, name, name_len);
    len += name_len;
    memcpy(buf + len, value, value_len);
    len += value_len;
    
    return len;
}

// Fallback: Execute PHP using CLI
static void execute_php_via_cli(int client_socket, const char *php_file) {
    printf("Using PHP CLI fallback for: %s\n", php_file);
    
    char command[1024];
    snprintf(command, sizeof(command), "php-cgi \"%s\"", php_file);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        perror("Failed to execute PHP via CLI");
        const char *error = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\n\r\n<h1>PHP Execution Failed</h1><p>CLI fallback also failed.</p>";
        send(client_socket, error, strlen(error), 0);
        return;
    }
    
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }
    
    pclose(fp);
}

// Simple FastCGI response reader (more robust)
static int read_fcgi_response(int fastcgi_sock, int client_socket) {
   FCGI_Header header;
    char buffer[4096];
    char response_buffer[65536] = {0};
    size_t total_bytes = 0;
    ssize_t bytes_read;

    // Read all FCGI_STDOUT packets
    while ((bytes_read = read(fastcgi_sock, &header, sizeof(header))) > 0) {
        if (bytes_read != sizeof(header)) {
            break;
        }

        uint16_t content_length = ntohs(header.content_length);
        uint8_t padding_length = header.padding_length;

        if (content_length > 0 && header.type == FCGI_STDOUT) {
            bytes_read = read(fastcgi_sock, buffer, content_length);
            if (bytes_read > 0) {
                if (total_bytes + bytes_read < sizeof(response_buffer)) {
                    memcpy(response_buffer + total_bytes, buffer, bytes_read);
                    total_bytes += bytes_read;
                }
            }
        }

        // Skip padding
        if (padding_length > 0) {
            lseek(fastcgi_sock, padding_length, SEEK_CUR);
        }

        if (header.type == FCGI_END_REQUEST) {
            break;
        }
    }

    if (total_bytes > 0) {
        // Find the start of the HTML body (after headers)
        char *body_start = strstr(response_buffer, "\r\n\r\n");
        if (body_start) {
            body_start += 4; // Move past "\r\n\r\n"
            size_t body_length = total_bytes - (body_start - response_buffer);
            http_send_response(client_socket, 200, "text/html", body_start, body_length);
        } else {
            // No headers found, assume entire response is body
            http_send_response(client_socket, 200, "text/html", response_buffer, total_bytes);
        }
        return 0;
    }

    return -1;
}

// Test PHP-FPM connection
static int test_php_fpm_connection() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 0;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FASTCGI_SOCK, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Cannot connect to PHP-FPM");
        close(sock);
        return 0;
    }
    
    printf("Connected to PHP-FPM successfully\n");
    close(sock);
    return 1;
}

void http_handle_php(int client_socket, httpreq *req, const char *request_data, const char *username) {
    printf("=== PHP HANDLER START ===\n");
    printf("PHP request: %s\n", req->url);
    printf("Method: %s\n", req->method);
    
    // Get current working directory
    char current_dir[512];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd() error");
        execute_php_via_cli(client_socket, req->url);
        return;
    }

    // Construct the absolute path to the PHP file
    char absolute_path[1024];
    snprintf(absolute_path, sizeof(absolute_path), "%s%s", current_dir, req->url);
    printf("Absolute path: %s\n", absolute_path);

    // Verify the file exists
    if (access(absolute_path, F_OK) != 0) {
        printf("PHP file not found: %s\n", absolute_path);
        const char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>File Not Found</h1>";
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Extract POST data early for debugging
    const char *post_data = NULL;
    size_t post_data_len = 0;
    
    if (strcmp(req->method, "POST") == 0) {
        printf("Processing POST request\n");
        const char *body_start = strstr(request_data, "\r\n\r\n");
        if (body_start) {
            body_start += 4; // Move past "\r\n\r\n"
            post_data = body_start;
            post_data_len = strlen(body_start);
            printf("POST data (%zu bytes): %.*s\n", post_data_len, (int)post_data_len, post_data);
        } else {
            printf("No POST body found in request\n");
        }
    }

    // Try FastCGI first if PHP-FPM is available
    if (test_php_fpm_connection()) {
        printf("PHP-FPM connection test successful\n");
        int fastcgi_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fastcgi_sock >= 0) {
            printf("FastCGI socket created successfully\n");
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, FASTCGI_SOCK, sizeof(addr.sun_path) - 1);
            
            if (connect(fastcgi_sock, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
                printf("Connected to PHP-FPM socket successfully\n");
                
                // Build FastCGI parameters
                char params_buf[4096];
                uint16_t params_len = 0;
                
                // Essential parameters
                params_len += encode_nv_pair(params_buf + params_len, "REQUEST_METHOD", req->method);
                params_len += encode_nv_pair(params_buf + params_len, "SCRIPT_FILENAME", absolute_path);
                params_len += encode_nv_pair(params_buf + params_len, "SCRIPT_NAME", req->url);
                params_len += encode_nv_pair(params_buf + params_len, "REQUEST_URI", req->url);
                params_len += encode_nv_pair(params_buf + params_len, "DOCUMENT_ROOT", current_dir);
                params_len += encode_nv_pair(params_buf + params_len, "SERVER_SOFTWARE", "CWebServer/1.0");
                params_len += encode_nv_pair(params_buf + params_len, "GATEWAY_INTERFACE", "FastCGI/1.0");
                params_len += encode_nv_pair(params_buf + params_len, "REMOTE_ADDR", "127.0.0.1");
                params_len += encode_nv_pair(params_buf + params_len, "SERVER_ADDR", "127.0.0.1");
                params_len += encode_nv_pair(params_buf + params_len, "SERVER_PORT", "8080");
                params_len += encode_nv_pair(params_buf + params_len, "SERVER_PROTOCOL", "HTTP/1.1");
                
                // Add CONTENT_LENGTH for POST requests
                if (strcmp(req->method, "POST") == 0 && post_data_len > 0) {
                    char content_length_str[64];
                    snprintf(content_length_str, sizeof(content_length_str), "%zu", post_data_len);
                    params_len += encode_nv_pair(params_buf + params_len, "CONTENT_LENGTH", content_length_str);
                    printf("Set CONTENT_LENGTH: %s\n", content_length_str);
                    
                    // Add CONTENT_TYPE
                    const char *content_type = find_header(request_data, "Content-Type:");
                    if (content_type) {
                        params_len += encode_nv_pair(params_buf + params_len, "CONTENT_TYPE", content_type);
                        printf("Set CONTENT_TYPE: %s\n", content_type);
                    } else {
                        // Default content type for form data
                        params_len += encode_nv_pair(params_buf + params_len, "CONTENT_TYPE", "application/x-www-form-urlencoded");
                        printf("Set default CONTENT_TYPE: application/x-www-form-urlencoded\n");
                    }
                }
                
                // Handle query string
                const char *query_param = strchr(req->url, '?');
                if (query_param) {
                    params_len += encode_nv_pair(params_buf + params_len, "QUERY_STRING", query_param + 1);
                    printf("Set QUERY_STRING: %s\n", query_param + 1);
                } else {
                    params_len += encode_nv_pair(params_buf + params_len, "QUERY_STRING", "");
                    printf("Set empty QUERY_STRING\n");
                }
                
                printf("Sending FastCGI parameters (%d bytes)\n", params_len);
                
                // Send FastCGI packets
                uint8_t begin_req_body[8] = {0, FCGI_RESPONDER, 0, 0, 0, 0, 0, 0};
                
                if (send_fcgi_packet(fastcgi_sock, FCGI_BEGIN_REQUEST, 1, (const char*)begin_req_body, 8) == 0) {
                    printf("Sent FCGI_BEGIN_REQUEST successfully\n");
                    
                    if (send_fcgi_packet(fastcgi_sock, FCGI_PARAMS, 1, params_buf, params_len) == 0) {
                        printf("Sent FCGI_PARAMS successfully\n");
                        
                        if (send_fcgi_packet(fastcgi_sock, FCGI_PARAMS, 1, "", 0) == 0) {
                            printf("Sent empty FCGI_PARAMS (end of params)\n");
                            
                            // Handle POST data if any
                            if (strcmp(req->method, "POST") == 0 && post_data_len > 0) {
                                printf("Sending POST data via FCGI_STDIN: %zu bytes\n", post_data_len);
                                if (send_fcgi_packet(fastcgi_sock, FCGI_STDIN, 1, post_data, post_data_len) != 0) {
                                    printf("Failed to send POST data\n");
                                } else {
                                    printf("POST data sent successfully\n");
                                }
                            }
                            
                            // Send empty FCGI_STDIN to signal end of data
                            if (send_fcgi_packet(fastcgi_sock, FCGI_STDIN, 1, "", 0) == 0) {
                                printf("Sent empty FCGI_STDIN (end of data)\n");
                                
                                // Read and process response
                                if (read_fcgi_response(fastcgi_sock, client_socket) == 0) {
                                    printf("FastCGI execution successful\n");
                                    close(fastcgi_sock);
                                    printf("=== PHP HANDLER END ===\n");
                                    return;
                                } else {
                                    printf("FastCGI response reading failed\n");
                                }
                            } else {
                                printf("Failed to send empty FCGI_STDIN\n");
                            }
                        } else {
                            printf("Failed to send empty FCGI_PARAMS\n");
                        }
                    } else {
                        printf("Failed to send FCGI_PARAMS\n");
                    }
                } else {
                    printf("Failed to send FCGI_BEGIN_REQUEST\n");
                }
                
                close(fastcgi_sock);
                printf("Closed FastCGI socket\n");
            } else {
                printf("Failed to connect to PHP-FPM socket\n");
                perror("Connect error");
            }
        } else {
            printf("Failed to create FastCGI socket\n");
            perror("Socket error");
        }
    } else {
        printf("PHP-FPM connection test failed\n");
    }
    
    // If FastCGI failed, use CLI fallback
    printf("FastCGI failed, using CLI fallback\n");
    execute_php_via_cli(client_socket, absolute_path);
    printf("=== PHP HANDLER END ===\n");
}

