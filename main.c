// main.c
// Main server logic and request handling.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <libgen.h> // For dirname
#include <sys/stat.h> // For mkdir
#include <sys/types.h>
#include <dirent.h>

#include "http_handler.h"
#include "session.h"
#include "auth.h"

#define PORT 8080
#define BUFFER_SIZE 8192

// Global error message buffer
char error_msg[256];

/**
 * @brief Checks if a request URL matches a specific path.
 * @param request_url The URL from the request.
 * @param path The path to check against.
 * @return true if the URL matches the path exactly, false otherwise.
 */
bool url_matches(const char *request_url, const char *path) {
    // Exact match, ignoring any query strings
    size_t path_len = strlen(path);
    if (strncmp(request_url, path, path_len) == 0) {
        // Check if the URL ends exactly after the path or is followed by a query string '?'
        if (request_url[path_len] == '\0' || request_url[path_len] == '?') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Thread function to handle a single client connection.
 * @param arg The client socket file descriptor.
 * @return NULL.
 */
void *handle_client(void *arg) {
    int c = *(int *)arg;
    free(arg);
    
    char *request = read_full_request(c);
    if (!request) {
        close(c);
        return NULL;
    }

    httpreq *req = parse_http(request);
    if (!req) {
        http_send_response(c, 400, "text/plain", "Bad Request", 11);
        free(request);
        close(c);
        return NULL;
    }

    printf("Received request: Method=%s, URL=%s\n", req->method, req->url);
    fflush(stdout);

    const char *session_id = get_session_id_from_request(request);
    Session *session = NULL;
    if (session_id) {
        session = get_session(session_id);
    }
    
    // Welcome Page and Login/Register Routing
    if (url_matches(req->url, "/")) {
        http_send_welcome_page(c);
    } else if (url_matches(req->url, "/login") && strcmp(req->method, "GET") == 0) {
        http_send_login_page(c);
    } else if (url_matches(req->url, "/register") && strcmp(req->method, "GET") == 0) {
        http_send_register_page(c);
    } else if (url_matches(req->url, "/login") && strcmp(req->method, "POST") == 0) {
        char username[64], password[64];
        const char *body = strstr(request, "\r\n\r\n") + 4;
        if (get_post_param(body, "username", username, sizeof(username)) &&
            get_post_param(body, "password", password, sizeof(password))) {
            char decoded_username[64], decoded_password[64];
            urldecode(decoded_username, username);
            urldecode(decoded_password, password);
            if (authenticate_user(decoded_username, decoded_password)) {
                Session *new_session = create_session(decoded_username);
                http_send_redirect_with_cookie(c, "/dashboard", new_session->session_id);
            } else {
                http_send_response(c, 401, "text/plain", "Unauthorized", 12);
            }
        } else {
            http_send_response(c, 400, "text/plain", "Bad Request", 11);
        }
    } else if (url_matches(req->url, "/register") && strcmp(req->method, "POST") == 0) {
        char username[64], password[64];
        const char *body = strstr(request, "\r\n\r\n") + 4;
        if (get_post_param(body, "username", username, sizeof(username)) &&
            get_post_param(body, "password", password, sizeof(password))) {
            char decoded_username[64], decoded_password[64];
            urldecode(decoded_username, username);
            urldecode(decoded_password, password);
            if (register_user(decoded_username, decoded_password)) {
                http_send_redirect(c, "/login"); // Redirect to login page on success
            } else {
                http_send_response(c, 400, "text/plain", "Registration failed. User may already exist.", 46);
            }
        } else {
            http_send_response(c, 400, "text/plain", "Bad Request", 11);
        }
    } else if (url_matches(req->url, "/dashboard")) {
        if (session && is_session_valid(session)) {
            http_send_dashboard(c, session->username);
        } else {
            http_send_welcome_page(c);
        }
    } else if (url_matches(req->url, "/logout") && strcmp(req->method, "POST") == 0) {
        if (session_id) {
            delete_session(session_id);
        }
        http_send_welcome_page(c);
    } else if (url_matches(req->url, "/upload") && strcmp(req->method, "POST") == 0) {
        if (session && is_session_valid(session)) {
            if (http_handle_upload(request, session->username)) {
                http_send_redirect(c, "/dashboard");
            } else {
                http_send_response(c, 500, "text/plain", "File Upload Failed", 18);
            }
        } else {
            http_send_welcome_page(c);
        }
    } else if (url_matches(req->url, "/delete_file") && strcmp(req->method, "POST") == 0) {
        if (session && is_session_valid(session)) {
            printf("Routing to http_handle_delete_file...\n");
            fflush(stdout);
            if (http_handle_delete_file(request, session->username)) {
                http_send_redirect(c, "/dashboard");
            } else {
                http_send_response(c, 500, "text/plain", "File Deletion Failed", 20);
            }
        } else {
            printf("DELETE request failed: Session is invalid or not found.\n");
            fflush(stdout);
            http_send_welcome_page(c);
        }
    } else if (url_matches(req->url, "/view_file")) {
        if (session && is_session_valid(session)) {
            printf("Routing to http_handle_view_file...\n");
            fflush(stdout);
            http_handle_view_file(c, req->url, session->username);
        } else {
            printf("VIEW request failed: Session is invalid or not found.\n");
            fflush(stdout);
            http_send_response(c, 401, "text/plain", "Unauthorized", 12);
        }
    } else if (url_matches(req->url, "/download_file")) {
        if (session && is_session_valid(session)) {
            http_send_file_for_download(c, req->url, session->username);
        } else {
            http_send_response(c, 401, "text/plain", "Unauthorized", 12);
        }
    } else {
        http_send_response(c, 404, "text/plain", "Not Found", 9);
    }

    free(req->method);
    free(req->url);
    free(req->protocol);
    free(req);
    free(request);
    close(c);
    return NULL;
}


/**
 * @brief Sets up and runs the server.
 */
int main() {
    int s;
    struct sockaddr_in server_addr;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(s);
        return EXIT_FAILURE;
    }

    if (listen(s, 5) < 0) {
        perror("Listen failed");
        close(s);
        return EXIT_FAILURE;
    }

    printf("Server listening on port %d...\n", PORT);
    fflush(stdout);

    while (1) {
        int *client_sock = malloc(sizeof(int));
        if (!client_sock) {
            perror("Failed to allocate memory for client socket");
            continue;
        }
        *client_sock = accept(s, NULL, NULL);
        if (*client_sock < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_sock) != 0) {
            perror("Thread creation failed");
            free(client_sock);
            close(*client_sock);
            continue;
        }
        pthread_detach(thread_id);
    }

    close(s);
    return EXIT_SUCCESS;
}
