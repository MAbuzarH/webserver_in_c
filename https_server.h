#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>

void initialize_openssl();
int create_https_listener(int port);
ssize_t ssl_send_wrapper(void* connection, const char* buffer, size_t length);
ssize_t ssl_recv_wrapper(void* connection, char* buffer, size_t length);
ssize_t send_wrapper(void* connection, const char* buffer, size_t length);
ssize_t recv_wrapper(void* connection, char* buffer, size_t length);
SSL_CTX* create_ssl_context(const char* cert_file,const char* key_file);
char* read_full_request_ssl(SSL* ssl, size_t* out_length);
SSL* accept_https_connection(SSL_CTX* ctx, int https_listen_sock);
#endif