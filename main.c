// main.c
//command to run program
//gcc main.c http_handler.c https_server.c session.c auth.c php_handler.c thread_safe.c -lssl -lcrypto -lpthread -luuid  -o server


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "http_handler.h"
#include "https_handler.h"
#include "https_server.h"
#include "thread_safe.h"
#include "php_handler.h"
#include "session.h"
#include "auth.h"

#define HTTP_PORT 8080
#define HTTPS_PORT 8443  // Use 443 for standard HTTPS if permitted
#define BUFFER_SIZE 8192

// Forward declarations for thread handler functions
void* handle_http_client(void* arg);
void* handle_https_client(void* arg);


/**
 * @brief Thread function to handle a single client connection.
 * @param arg The client socket file descriptor.
 * @return NULL.
 */
void handle_client(int c) {
    // Read the full HTTP request from the client socket.
    // int c = *((int*)arg);
    // free(arg); // Free the memory allocated in the main thread

    // Check for a valid socket before proceeding
    if (c < 0) {
        printf("Invalid client socket.\n");
        return;
    } 
   size_t *request_length = 0;
    // char *request = read_full_request(c,&request_length);
    // char *request = read_full_request(c,&request_length);
    char *request = read_full_request(c,request_length);
    if (!request) {
        printf("Failed to read request.\n");
        close(c);
        return;
    }

    // Parse the raw request string into a structured httpreq object.
    httpreq *req = parse_http(request);
    if (!req) {
        printf("Failed to parse request.\n");
        free(request);
        return ;
    }

    // Extract the session ID from the request headers to authenticate the user.
    const char *session_id = get_session_id_from_request(request);
    Session *session = NULL;
    if (session_id) {
         session = get_session_ts(session_id);
        //session = get_session(session_id);
    }
    
    // Log the received request for debugging purposes.
    printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

  


    // Route a POST request to create a new folder.
    if (strcmp(req->url, "/create_folder") == 0 && strcmp(req->method, "POST") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            // Call the handler to create the folder based on the POST data.
            char path[256] = "/";
            char foldername[256];
            const char *body = strstr(request, "\r\n\r\n") + 4;
          //if(  get_post_param(body, "path", path, sizeof(path)) &&
          //  get_post_param(body, "foldername", foldername, sizeof(foldername))){
            if (handle_create_folder(request, session->username)) {
                // On success, redirect the user back to the current directory.
               
                char redirect_url[640];
                 // normalize_path(redirect_url, path, foldername); // Use your new function
                 //http_send_redirect(c, redirect_url);
                 snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
                 http_send_redirect(c, redirect_url);
            } else {
                // Failure: Redirect back to the original path
        // char redirect_url[640];
        // snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
        // http_send_redirect(c, redirect_url);
                // char redirect_url[300];
                http_send_response(c, 500, "text/plain", "Failed to create folder", 24);
            }
       // }else{
          //  printf("handle_client():failed to find %s or %s :Cre_fol \n",path,foldername);
          //  fflush(stdout);
       // }
        }
    }
    // Route a POST request to delete a folder.
    else if (strcmp(req->url, "/delete_folder") == 0 && strcmp(req->method, "POST") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            // Call the handler to delete the folder based on the POST data.
            if (http_handle_delete_folder(request, session->username)) {
                // On success, redirect the user back to the parent directory.
                char path[256] = "/";
                // char *foldername;
                const char *body = strstr(request, "\r\n\r\n") + 4;
                get_post_param(body, "path", path, sizeof(path));
                //get_post_param(body, "foldername", foldername, sizeof(foldername));
                
                  // Check if the current path is the root directory
    //           if (strcmp(path, "/") == 0) {
    //            snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s%s", path, foldername);
    //           } else {
    //     // Concatenate with a forward slash for nested folders
    //           snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s/%s", path, foldername);
    // }
                char redirect_url[300];
                snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
                http_send_redirect(c, redirect_url);
            } else {
                http_send_response(c, 500, "text/plain", "Failed to delete folder", 24);
            }
        }
    }
    // Route requests to the dashboard page. This includes the base URL and URLs with a path parameter.
    else if (strstr(req->url, "/dashboard") == req->url) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            // Extract the 'path' parameter from the URL query string.
            char path[256] = "/";
            const char *path_param = strstr(req->url, "path=");
            if (path_param) {
                // Decode the URL-encoded path before using it.
                urldecode(path, path_param + strlen("path="));
            }
            // Pass the parsed path to the dashboard handler.
            http_send_dashboard(c, session->username, path);
        }
    }

    // --- EXISTING ROUTING LOGIC ---

    // Route a GET request for the root page.
    else if (strcmp(req->url, "/") == 0 && strcmp(req->method, "GET") == 0) {
        if (session) {
            http_send_redirect(c, "/dashboard?path=/");
        } else {
            http_send_welcome_page(c);
        }
    } 
    // Route a GET request for the login page.
    else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "GET") == 0) {
        http_send_login_page(c);
    } 
    // Route a POST request for login authentication.
    else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
        char username[MAX_USERNAME_LENGTH + 1], password[256];
        const char *body = strstr(request, "\r\n\r\n") + 4;
        if (get_post_param(body, "username", username, sizeof(username)) &&
            get_post_param(body, "password", password, sizeof(password))) {
            if (authenticate_user(username, password)) {
                Session *new_session = create_session_ts(username);
                http_send_redirect_with_cookie(c, "/dashboard?path=/", new_session->session_id);
            } else {
                http_send_response(c, 401, "text/html", "<h1>Unauthorized</h1><p>Incorrect username or password.</p>", 60);
            }
        } else {
            http_send_response(c, 400, "text/plain", "Bad Request", 11);
        }
    }
    // Route a GET request for the registration page.
    else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "GET") == 0) {
        http_send_register_page(c);
    }
    // Route a POST request for user registration.
    else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
        char username[MAX_USERNAME_LENGTH + 1], password[256];
        const char *body = strstr(request, "\r\n\r\n") + 4;
        if (get_post_param(body, "username", username, sizeof(username)) &&
            get_post_param(body, "password", password, sizeof(password))) {
            if (register_user(username, password)) {
                http_send_redirect(c, "/login");
            } else {
                http_send_response(c, 500, "text/html", "<h1>Error</h1><p>Failed to register user.</p>", 47);
            }
        } else {
            http_send_response(c, 400, "text/plain", "Bad Request", 11);
        }
    }
    // Route a POST request for file upload.
   // In handle_client function, replace the upload handling:
else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
    if (!session) {
        http_send_redirect(c, "/login");
    } else {
        const char *content_length_str = find_header(request, "Content-Length:");
        int content_length = content_length_str ? atoi(content_length_str) : 0;
        
        printf("Processing file upload, content length: %d\n", content_length);
        
        // Get the actual request length
        size_t request_len = strlen(request); // This will be updated in read_full_request
        
        // For all file types, use the same upload handler
        if (http_handle_upload(request, session->username)) {
            // Extract path for redirect
            char path[256] = "/";
            const char *body = strstr(request, "\r\n\r\n");
            if (body) {
                body += 4;
                get_post_param(body, "path", path, sizeof(path));
            }
            
            char redirect_url[300];
            snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
            http_send_redirect(c, redirect_url);
        } else {
            http_send_response(c, 500, "text/plain", "Upload failed", 13);
        }
    }
}

    // Route a POST request for file deletion.
    else if (strcmp(req->url, "/delete_file") == 0 && strcmp(req->method, "POST") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            if (http_handle_delete_file(request, session->username)) {
                char path[256] = "/";
                const char *body = strstr(request, "\r\n\r\n") + 4;
                get_post_param(body, "path", path, sizeof(path));
                char redirect_url[300];
                snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
                http_send_redirect(c, redirect_url);
            } else {
                http_send_response(c, 500, "text/plain", "Failed to delete file", 22);
            }
        }
    }
    // Route a POST request for logout.
    else if (strcmp(req->url, "/logout") == 0 && strcmp(req->method, "POST") == 0) {
        if (session) {
            delete_session_ts(session->session_id);
        }
        http_send_redirect(c, "/");
    }
     //Get request to handel get for php file
    // else if(has_file_extension(req->url,".php")){
    //  if(!session){
    //  http_send_redirect(c, "/login"); 
    //  }else{
    //     http_handle_php(c,req,request,session->username);
    //  }
    // }
    else if (strstr(req->url, "/user_files/") == req->url) {
    // If the URL is a request to a user's file, handle it here.
    if (!session) {
        http_send_redirect(c, "/login");
    } else {
        if (has_file_extension(req->url, ".php")) {
            // It's a PHP script, run the PHP handler
            http_handle_php(c, req, request, session->username);
        } else {
            // It's a static file, run the static file handler
            http_handle_view_file(c, req->url, session->username);
        }
    }
}
    // Route a GET request to view a file.
    else if (strstr(req->url, "/view_file") == req->url && strcmp(req->method, "GET") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            if(has_file_extension(req->url,".php")){
               http_handle_php(c,req,request,session->username);
            }else{
                http_handle_view_file(c, req->url, session->username);
            }
                 
            
        }
    }
    // Route a GET request to download a file.
    else if (strstr(req->url, "/download_file") == req->url && strcmp(req->method, "GET") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            http_send_file_for_download(c, req->url, session->username);
        }
    }
   
    // Handle all other unrecognized requests with a 404 Not Found response.
    else {
        http_send_response(c, 404, "text/plain", "Not Found", 9);
    }
    
    // Clean up allocated memory.
    free(req->method);
    free(req->url);
    free(req->protocol);
    free(req);
    free(request);
    close(c);
}

/**
 * @brief Checks if a request URL matches a specific path.
 * @param request_url The URL from the request.
 * @param path The path to check against.
 * @return true if the URL matches the path exactly, false otherwise.
 */
bool url_matches(const char *request_url, const char *path) {
    size_t path_len = strlen(path);
    return (strncmp(request_url, path, path_len) == 0 &&
           (request_url[path_len] == '\0' || request_url[path_len] == '?'));
}

int main() {
    int http_sock, https_sock;
    struct sockaddr_in server_addr;
    
    // Initialize OpenSSL
    initialize_openssl();
    
    // Create SSL context
    SSL_CTX* ssl_ctx = create_ssl_context("cert.pem", "key.pem");
    if (!ssl_ctx) {
        fprintf(stderr, "Failed to create SSL context\n");
        return EXIT_FAILURE;
    }
    
    // Create HTTP socket
    http_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (http_sock < 0) {
        perror("HTTP socket creation failed");
        SSL_CTX_free(ssl_ctx);
        return EXIT_FAILURE;
    }
    
    int reuse = 1;
    setsockopt(http_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(HTTP_PORT);
    
    if (bind(http_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("HTTP bind failed");
        SSL_CTX_free(ssl_ctx);
        close(http_sock);
        return EXIT_FAILURE;
    }
    
    if (listen(http_sock, 5) < 0) {
        perror("HTTP listen failed");
        SSL_CTX_free(ssl_ctx);
        close(http_sock);
        return EXIT_FAILURE;
    }
    
    // Create HTTPS listening socket using helper function
    https_sock = create_https_listener(HTTPS_PORT);
    if (https_sock < 0) {
        fprintf(stderr, "Failed to create HTTPS listener socket\n");
        SSL_CTX_free(ssl_ctx);
        close(http_sock);
        return EXIT_FAILURE;
    }

    printf("Server listening on HTTP port %d and HTTPS port %d\n", HTTP_PORT, HTTPS_PORT);
    fflush(stdout);
    
    fd_set readfds;
    int max_fd = (http_sock > https_sock) ? http_sock : https_sock;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(http_sock, &readfds);
        FD_SET(https_sock, &readfds);
        
        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select error");
            continue;
        }
        
        // Handle HTTP connection
        if (FD_ISSET(http_sock, &readfds)) {
            int* client_sock = malloc(sizeof(int));
            if (!client_sock) {
                perror("Failed to allocate memory for HTTP client socket");
                continue;
            }
            *client_sock = accept(http_sock, NULL, NULL);
            if (*client_sock < 0) {
                perror("HTTP accept failed");
                free(client_sock);
                continue;
            }
            pthread_t tid;
            pthread_create(&tid, NULL, handle_http_client, client_sock);
            pthread_detach(tid);
        }
        
        // Handle HTTPS connection
        if (FD_ISSET(https_sock, &readfds)) {
            SSL* ssl_client = accept_https_connection(ssl_ctx, https_sock);
            if (!ssl_client) {
                fprintf(stderr, "SSL accept failed for HTTPS client\n");
                continue;
            }
            pthread_t tid;
            pthread_create(&tid, NULL, handle_https_client, ssl_client);
            pthread_detach(tid);
        }
    }
    
    close(http_sock);
    close(https_sock);
    SSL_CTX_free(ssl_ctx);
    return EXIT_SUCCESS;
}

// Thread function for handling HTTP connections (existing code)
void* handle_http_client(void* arg) {
    int client_sock = *((int*)arg);
    free(arg);
    handle_client(client_sock);  // Reuse existing handle_client with socket
    return NULL;
}

// Thread function for handling HTTPS connections
void* handle_https_client(void* arg) {
    SSL* ssl = (SSL*)arg;

    size_t request_length = 0;
    char* request = read_full_request_ssl(ssl, &request_length);
    if (!request) {
        SSL_shutdown(ssl);
        int sockfd = SSL_get_fd(ssl);
        SSL_free(ssl);
        close(sockfd);
        return NULL;
    }

    httpreq* req = parse_http(request);
    if (!req) {
        free(request);
        SSL_shutdown(ssl);
        int sockfd = SSL_get_fd(ssl);
        SSL_free(ssl);
        close(sockfd);
        return NULL;
    }

     const char *session_id = get_session_id_from_request(request);
    Session *session = NULL;
    if (session_id) {
         session = get_session_ts(session_id);
        //session = get_session(session_id);
    }
    
    // Log the received request for debugging purposes.
    printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

    // For demonstration, simple GET "/" handling
    if (strcmp(req->url, "/") == 0 && strcmp(req->method, "GET") == 0) {
          https_send_welcome_page(ssl);
       
        // https_send_redirect(ssl,"/login");
        // const char* response_body = "<html><body><h1>Welcome to the HTTPS server!</h1></body></html>";
        // char header[512];
        // snprintf(header, sizeof(header),
        //          "HTTP/1.1 200 OK\r\n"
        //          "Content-Type: text/html\r\n"
        //          "Content-Length: %zu\r\n"
        //          "Connection: close\r\n"
        //          "\r\n",
        //          strlen(response_body));

        // // Send headers
        // ssl_send_wrapper(ssl, header, strlen(header));
        // // Send body
        // ssl_send_wrapper(ssl, response_body, strlen(response_body));
    } else {
        // Handle other routes or send 404
        const char* response_body = "Not Found";
        char header[512];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 strlen(response_body));

        ssl_send_wrapper(ssl, header, strlen(header));
        ssl_send_wrapper(ssl, response_body, strlen(response_body));
    }

    free(req->method);
    free(req->url);
    free(req->protocol);
    free(req);

    free(request);

    SSL_shutdown(ssl);
    int sockfd = SSL_get_fd(ssl);
    SSL_free(ssl);
    close(sockfd);
    return NULL;
}

