#include "https_server.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h> 
#include<ctype.h>



void initialize_openssl(){
    // Initialize the OpenSSL library for encryption/decryption algorithms
    SSL_library_init();

    // Load all the necessary error messages for debugging
    SSL_load_error_strings();

    // Load all available cryptographic algorithms (hash, ciphers, etc.)
    OpenSSL_add_all_algorithms();

    // Optionally, seed the PRNG (pseudorandom number generator)
    // This might be handled internally but sometimes needs explicit seeding
    // RAND_load_file("/dev/urandom", 1024); // On UNIX-like systems
}

SSL_CTX* create_ssl_context(const char* cert_file,const char* key_file){
    // Use TLS server method that automatically negotiates highest supported TLS version
    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);

   
    if(!ctx){
        fprintf(stderr,"Unable to create SSL context \n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // Set options to improve security (disable SSLv2 and SSLv3)
    SSL_CTX_set_options(ctx,SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    
    //Load server certifaction
    if(SSL_CTX_use_certificate_file(ctx,cert_file,SSL_FILETYPE_PEM)<= 0){
        fprintf(stderr, "Error loading certificate from file: %s\n", cert_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    } 

    //load server private key 
    if(SSL_CTX_use_PrivateKey_file(ctx,key_file,SSL_FILETYPE_PEM)<=0){
        fprintf(stderr, "Error loading private key from file: %s\n", key_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match the certificate public key\n");
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

int create_https_listener(int port){
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
     if (sockfd < 0) {
        perror("Failed to create socket");
        return -1;
    }

    int reuse = 1;
    setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(sockfd, (struct sockaddr*)&addr,sizeof(addr))<0){
        perror("Bind failed");
        close(sockfd);
        return -1;
    }
    
    if(listen(sockfd,10)<0){
     perror("listen failed");
     close(sockfd);
     return -1;
    }
    return sockfd; 
}

/**
 * Accept an SSL/TLS connection:
 *  - Accepts a plain TCP connection from the HTTPS listen socket
 *  - Creates an SSL object using the context
 *  - Associates the socket with the SSL
 *  - Performs SSL handshake
 * 
 * Returns:
 *   On success, returns a pointer to the SSL object (caller must free).
 *   On failure, returns NULL.
 */

SSL* accept_https_connection(SSL_CTX* ctx, int https_listen_sock) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Accept a new TCP connection
    int client_sock = accept(https_listen_sock, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock < 0) {
        perror("Accept failed");
        return NULL;
    }

    // Create a new SSL structure for the connection
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "Unable to create SSL structure\n");
        close(client_sock);
        return NULL;
    }

    // Bind the SSL object with the accepted socket descriptor
    SSL_set_fd(ssl, client_sock);

    // Perform SSL/TLS handshake
    if (SSL_accept(ssl) <= 0) {
        fprintf(stderr, "SSL accept error:\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(client_sock);
        return NULL;
    }

    // Handshake successful: return SSL pointer to caller
    return ssl;
}

// Wrapper for plain socket recv()
ssize_t recv_wrapper(void* connection, char* buffer, size_t length) {
    int sockfd = *((int*)connection);
    return recv(sockfd, buffer, length, 0);
}

// Wrapper for plain socket send()
ssize_t send_wrapper(void* connection, const char* buffer, size_t length) {
    int sockfd = *((int*)connection);
    return send(sockfd, buffer, length, 0);
}

// Wrapper for SSL_read()
ssize_t ssl_recv_wrapper(void* connection, char* buffer, size_t length) {
    SSL* ssl = (SSL*)connection;
    int ret;
    while (1) {
        ret = SSL_read(ssl, buffer, (int)length);
        if (ret > 0) {
            return ret;
        }
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Retry on these; could add select/poll here for non-blocking sockets
            continue;
        }
        // Failure or EOF
        return -1;
    }
}

// Wrapper for SSL_write()
ssize_t ssl_send_wrapper(void* connection, const char* buffer, size_t length) {
    SSL* ssl = (SSL*)connection;
    int ret;
    size_t total_sent = 0;
    while (total_sent < length) {
        ret = SSL_write(ssl, buffer + total_sent, (int)(length - total_sent));
        if (ret > 0) {
            total_sent += ret;
        } else {
            int err = SSL_get_error(ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // Retry on these; could add select/poll here
                continue;
            }
            // Failure
            return -1;
        }
    }
    return (ssize_t)total_sent;
}

// Reads full HTTP request from SSL socket into a dynamically allocated buffer.
// Parameters:
//   ssl          - pointer to SSL object
//   out_length   - pointer to size_t to store length of returned buffer
// Returns:
//   A malloced pointer with request data (caller must free), or NULL on failure.
// Returns malloc'ed buffer with length stored in out_length (or NULL)
char* read_full_request_ssl(SSL* ssl, size_t* out_length) {
    size_t cap = 8192;
    char* buf = malloc(cap);
    if (!buf) return NULL;
    size_t total = 0;

    // Helper lambda-like function: search for CRLFCRLF in buf[0..total)
    #define FIND_HDR_END(buf, total) \
        ({ size_t _i = 0; ssize_t _pos = -1; \
           for (_i = 0; _i + 3 < (total); ++_i) { \
               if (buf[_i] == '\r' && buf[_i+1] == '\n' && buf[_i+2] == '\r' && buf[_i+3] == '\n') { _pos = (ssize_t)_i; break; } } _pos; })

    ssize_t hdr_end_pos = -1;

    // Read until we find header end
    while (1) {
        if (total + 4096 > cap) {
            size_t newcap = cap * 2;
            char* n = realloc(buf, newcap);
            if (!n) { free(buf); return NULL; }
            buf = n; cap = newcap;
        }

        int bytes = SSL_read(ssl, buf + total, (int)(cap - total));
        if (bytes <= 0) {
            int err = SSL_get_error(ssl, bytes);
            // treat WANT_READ/WANT_WRITE as retry; otherwise error
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // short sleep to avoid busy loop
                usleep(1000);
                continue;
            }
            free(buf);
            return NULL;
        }
        total += bytes;

        hdr_end_pos = FIND_HDR_END(buf, total);
        if (hdr_end_pos >= 0) break;
        // safety: limit header size to say 64KB
        if (total > 64 * 1024) { free(buf); return NULL; }
    }

    size_t header_len = (size_t)hdr_end_pos + 4;
    // find Content-Length header in a case-insensitive way (simple approach)
    size_t content_length = 0;
    for (size_t i = 0; i + 15 < header_len; ++i) {
        // compare case-insensitive for "content-length:"
        if ((buf[i] == 'C' || buf[i] == 'c') &&
            (buf[i+1] == 'o' || buf[i+1] == 'O')) {
            // naive check - better to parse headers properly afterwards
            if (strncasecmp(buf + i, "Content-Length:", 15) == 0) {
                // parse digits after the colon
                char tmp[32] = {0};
                size_t j = i + 15;
                while (j < header_len && (buf[j] == ' ' || buf[j] == '\t')) ++j;
                size_t k = 0;
                while (j < header_len && k + 1 < sizeof(tmp) && isdigit((unsigned char)buf[j])) {
                    tmp[k++] = buf[j++];
                }
                tmp[k] = '\0';
                content_length = (size_t)atoi(tmp);
                break;
            }
        }
    }

    // Read remaining body bytes if any
    size_t body_already = total - header_len;
    size_t body_needed = (content_length > body_already) ? (content_length - body_already) : 0;
    while (body_needed > 0) {
        if (total + 4096 > cap) {
            size_t newcap = cap * 2;
            char* n = realloc(buf, newcap);
            if (!n) { free(buf); return NULL; }
            buf = n; cap = newcap;
        }
        int bytes = SSL_read(ssl, buf + total, (int)(cap - total));
        if (bytes <= 0) {
            int err = SSL_get_error(ssl, bytes);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                usleep(1000);
                continue;
            }
            free(buf);
            return NULL;
        }
        total += bytes;
        if (bytes <= (int)body_needed) body_needed -= bytes;
        else body_needed = 0;
    }

    // Null terminate for convenience (buf[total] reserved)
    char* n = realloc(buf, total + 1);
    if (n) { buf = n; }
    buf[total] = '\0';
    if (out_length) *out_length = total;
    return buf;
    #undef FIND_HDR_END
}
