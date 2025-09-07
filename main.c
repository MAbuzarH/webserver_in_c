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
#include "php_handler.h"
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
    // Read the full HTTP request from the client socket.
    int c = *((int*)arg);
    free(arg); // Free the memory allocated in the main thread

    // Check for a valid socket before proceeding
    if (c < 0) {
        printf("Invalid client socket.\n");
        return NULL;
    } 

    char *request = read_full_request(c);
    if (!request) {
        printf("Failed to read request.\n");
        close(c);
        return NULL;
    }

    // Parse the raw request string into a structured httpreq object.
    httpreq *req = parse_http(request);
    if (!req) {
        printf("Failed to parse request.\n");
        free(request);
        return NULL;
    }

    // Extract the session ID from the request headers to authenticate the user.
    const char *session_id = get_session_id_from_request(request);
    Session *session = NULL;
    if (session_id) {
        session = get_session(session_id);
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
                Session *new_session = create_session(username);
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
    else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
        if (!session) {
            http_send_redirect(c, "/login");
        } else {
            if (http_handle_upload(c, request, session->username)) {
                char path[256] = "/";
                const char *body = strstr(request, "\r\n\r\n") + 4;
                get_post_param(body, "path", path, sizeof(path));
                char redirect_url[300];
                snprintf(redirect_url, sizeof(redirect_url), "/dashboard?path=%s", path);
                http_send_redirect(c, redirect_url);
            } else {
                http_send_response(c, 500, "text/plain", "File upload failed", 19);
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
            delete_session(session->session_id);
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

//   if(strcmp(req->url,".php")){
//         if (!session) {
//             http_send_redirect(c, "/login");
//         } else{
//         printf("routing to php request...\n");
//         http_handle_php_request(c, req->url,session->username);
//         }
       
//     }
