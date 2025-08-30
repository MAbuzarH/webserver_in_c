// main.c
// Main server logic: handles incoming connections and dispatches requests.

#include <stdio.h>      // Standard input/output library
#include <stdlib.h>     // Standard library for functions like exit()
#include <netinet/in.h> // Sockets library for internet addresses
#include <sys/types.h>  // System data types
#include <fcntl.h>      // File control options
#include <sys/socket.h> // Core sockets library
#include <unistd.h>     // POSIX API, includes fork()
#include <string.h>     // String manipulation functions
#include <arpa/inet.h>  // For inet_addr
#include <errno.h>      // For error codes like EAGAIN
#include <time.h>       // For getting the current time
#include <sys/time.h>   // for timeval struct
#include <sys/stat.h>   // For stat()
#include <sys/wait.h>


#include "session.h"
#include "http_handler.h"
#include "auth.h"


#define PORT 8080
#define MAX_CONNECTIONS 10
#define MAX_SESSIONS 100

char error_msg[256];


/**
 * Handles an incoming HTTP request.
 */
void handle_request(int c) {
    char* request = read_full_request(c);
    if (!request) {
        printf("Error: Failed to read request. %s\n", error_msg);
        http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
        close(c);
        return;
    }
    
    httpreq* req = parse_http(request);
    if (!req) {
        printf("Error: Failed to parse request. %s\n", error_msg);
        http_send_response(c, 400, "text/plain", "Bad Request", 11);
        free(request);
        close(c);
        return;
    }

    const char* session_id = get_session_id_from_request(request);
    Session* session = NULL;
    
    if (session_id) {
        session = find_session(session_id);
    }

    if (session) {
        // Authenticated routes
        if (strcmp(req->url, "/") == 0) {
            http_send_dashboard(c, session->username);
        } else if (strcmp(req->url, "/logout") == 0 && strcmp(req->method, "POST") == 0) {
            delete_session(session_id);
            http_send_redirect(c, "/login");
        } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
            http_handle_upload(request, session->username);
            http_send_redirect(c, "/");
        } else if (strcmp(req->url, "/delete_file") == 0 && strcmp(req->method, "POST") == 0) {
            http_handle_delete_file(request, session->username);
            http_send_redirect(c, "/");
        } else if (strncmp(req->url, "/user_files/", 12) == 0) {
            // Serve static files from user's directory
            char file_path[512]; // Increased buffer size to fix truncation warning
            snprintf(file_path, sizeof(file_path), ".%s", req->url);
            
            // Security check: prevent directory traversal
            if (strstr(file_path, "..")) {
                http_send_response(c, 403, "text/plain", "Forbidden", 9);
            } else {
                File* file_content = fileread(file_path);
                if (file_content) {
                    http_send_response(c, 200, get_content_type(file_content->filename), file_content->fc, file_content->size);
                    free_file(file_content);
                } else {
                    http_send_response(c, 404, "text/plain", "File Not Found", 14);
                }
            }
        } else {
            http_send_response(c, 404, "text/plain", "Not Found", 9);
        }
    } else {
        // Public/Authentication routes
        if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "GET") == 0) {
            http_send_login_page(c);
        } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
            const char* body_start = strstr(request, "\r\n\r\n");
            if (body_start) {
                body_start += 4;
                LoginData login_data;
                if (parse_login_data(body_start, &login_data) && authenticate_user(login_data.username, login_data.password)) {
                    char* new_session_id = create_session(login_data.username);
                    if (new_session_id) {
                        http_send_redirect_with_cookie(c, "/", new_session_id);
                    } else {
                        http_send_response(c, 500, "text/plain", "Failed to create session", 24);
                    }
                } else {
                    http_send_response(c, 401, "text/plain", "Unauthorized", 12);
                }
            } else {
                http_send_response(c, 400, "text/plain", "Bad Request", 11);
            }
        } else {
            // Redirect any unauthenticated user to the login page
            http_send_redirect(c, "/login");
        }
    }

    free(req);
    free(request);
    close(c);
}

/**
 * The main server loop.
 */
int main() {
    // Create the 'user_files' directory if it doesn't exist
    mkdir("user_files", 0777);
    
    int s;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("Socket creation failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(s, 5) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        int c = accept(s, (struct sockaddr *)&server_addr, &addr_len);
        if (c < 0) {
            perror("Accept failed");
            continue;
        }
        handle_request(c);
    }
    
    close(s);
    return 0;
}


// /**
//  * Handles an incoming HTTP request.
//  */
// void handle_request(int c) {
//     char* request = read_full_request(c);
//     if (!request) {
//         printf("Error: Failed to read request. %s\n", error_msg);
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         close(c);
//         return;
//     }
    
//     httpreq* req = parse_http(request);
//     if (!req) {
//         printf("Error: Failed to parse request. %s\n", error_msg);
//         http_send_response(c, 400, "text/plain", "Bad Request", 11);
//         free(request);
//         close(c);
//         return;
//     }

//     const char* session_id = get_session_id_from_request(request);
//     Session* session = NULL;
    
//     if (session_id) {
//         session = find_session(session_id);
//     }

//     if (session) {
//         // Authenticated routes
//         if (strcmp(req->url, "/") == 0) {
//             http_send_dashboard(c, session->username);
//         } else if (strcmp(req->url, "/logout") == 0 && strcmp(req->method, "POST") == 0) {
//             delete_session(session_id);
//             http_send_redirect(c, "/login");
//         } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//             http_handle_upload(request, session->username);
//             http_send_redirect(c, "/");
//         } else if (strcmp(req->url, "/delete_file") == 0 && strcmp(req->method, "POST") == 0) {
//             http_handle_delete_file(request, session->username);
//             http_send_redirect(c, "/");
//         } else if (strncmp(req->url, "/user_files/", 12) == 0) {
//             // Serve static files from user's directory
//             char file_path[256];
//             snprintf(file_path, sizeof(file_path), ".%s", req->url);
            
//             // Security check: prevent directory traversal
//             if (strstr(file_path, "..")) {
//                 http_send_response(c, 403, "text/plain", "Forbidden", 9);
//             } else {
//                 File* file_content = fileread(file_path);
//                 if (file_content) {
//                     http_send_response(c, 200, get_content_type(file_content->filename), file_content->fc, file_content->size);
//                     free_file(file_content);
//                 } else {
//                     http_send_response(c, 404, "text/plain", "File Not Found", 14);
//                 }
//             }
//         } else {
//             http_send_response(c, 404, "text/plain", "Not Found", 9);
//         }
//     } else {
//         // Public/Authentication routes
//         if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "GET") == 0) {
//             http_send_login_page(c);
//         } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//             const char* body_start = strstr(request, "\r\n\r\n");
//             if (body_start) {
//                 body_start += 4;
//                 LoginData login_data;
//                 if (parse_login_data(body_start, &login_data) && authenticate_user(login_data.username, login_data.password)) {
//                     char* new_session_id = create_session(login_data.username);
//                     if (new_session_id) {
//                         http_send_redirect_with_cookie(c, "/", new_session_id);
//                     } else {
//                         http_send_response(c, 500, "text/plain", "Failed to create session", 24);
//                     }
//                 } else {
//                     http_send_response(c, 401, "text/plain", "Unauthorized", 12);
//                 }
//             } else {
//                 http_send_response(c, 400, "text/plain", "Bad Request", 11);
//             }
//         } else {
//             // Redirect any unauthenticated user to the login page
//             http_send_redirect(c, "/login");
//         }
//     }

//     free(req);
//     free(request);
//     close(c);
// }

// /**
//  * The main server loop.
//  */
// int main() {
//     // Create the 'user_files' directory if it doesn't exist
//     mkdir("user_files", 0777);
    
//     int s;
//     struct sockaddr_in server_addr;
//     socklen_t addr_len = sizeof(struct sockaddr_in);

//     s = socket(AF_INET, SOCK_STREAM, 0);
//     if (s < 0) {
//         perror("Socket creation failed");
//         return 1;
//     }

//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(PORT);
//     server_addr.sin_addr.s_addr = INADDR_ANY;

//     if (bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         return 1;
//     }

//     if (listen(s, 5) < 0) {
//         perror("Listen failed");
//         return 1;
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         int c = accept(s, (struct sockaddr *)&server_addr, &addr_len);
//         if (c < 0) {
//             perror("Accept failed");
//             continue;
//         }
//         handle_request(c);
//     }
    
//     close(s);
//     return 0;
// }





// /**
//  * Handles an incoming HTTP request.
//  */
// void handle_request(int c) {
//     char* request = read_full_request(c);
//     if (!request) {
//         printf("Error: Failed to read request. %s\n", error_msg);
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         close(c);
//         return;
//     }
    
//     httpreq* req = parse_http(request);
//     if (!req) {
//         printf("Error: Failed to parse request. %s\n", error_msg);
//         http_send_response(c, 400, "text/plain", "Bad Request", 11);
//         free(request);
//         close(c);
//         return;
//     }

//     const char* session_id = get_session_id_from_request(request);
//     Session* session = NULL;
    
//     if (session_id) {
//         session = find_session(session_id);
//     }

//     if (session) {
//         // Authenticated routes
//         if (strcmp(req->url, "/") == 0) {
//             http_send_dashboard(c, session->username);
//         } else if (strcmp(req->url, "/logout") == 0 && strcmp(req->method, "POST") == 0) {
//             delete_session(session_id);
//             http_send_redirect(c, "/login");
//         } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//             http_handle_upload(request, session->username);
//             http_send_redirect(c, "/");
//         } else if (strcmp(req->url, "/delete_file") == 0 && strcmp(req->method, "POST") == 0) {
//             http_handle_delete_file(request, session->username);
//             http_send_redirect(c, "/");
//         } else if (strncmp(req->url, "/user_files/", 12) == 0) {
//             // Serve static files from user's directory
//             char file_path[256];
//             snprintf(file_path, sizeof(file_path), ".%s", req->url);
            
//             // Security check: prevent directory traversal
//             if (strstr(file_path, "..")) {
//                 http_send_response(c, 403, "text/plain", "Forbidden", 9);
//             } else {
//                 File* file_content = fileread(file_path);
//                 if (file_content) {
//                     http_send_response(c, 200, get_content_type(file_content->filename), file_content->fc, file_content->size);
//                     free_file(file_content);
//                 } else {
//                     http_send_response(c, 404, "text/plain", "File Not Found", 14);
//                 }
//             }
//         } else {
//             http_send_response(c, 404, "text/plain", "Not Found", 9);
//         }
//     } else {
//         // Public/Authentication routes
//         if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "GET") == 0) {
//             http_send_login_page(c);
//         } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//             if (authenticate_user(request)) {
//                 const char* username = get_username_from_request(request);
//                 char* new_session_id = create_session(username);
//                 if (new_session_id) {
//                     http_send_redirect_with_cookie(c, "/", new_session_id);
//                 } else {
//                     http_send_response(c, 500, "text/plain", "Failed to create session", 24);
//                 }
//             } else {
//                 http_send_response(c, 401, "text/plain", "Unauthorized", 12);
//             }
//         } else {
//             // Redirect any unauthenticated user to the login page
//             http_send_redirect(c, "/login");
//         }
//     }

//     free(req);
//     free(request);
//     close(c);
// }

// /**
//  * The main server loop.
//  */
// int main() {
//     // Create the 'user_files' directory if it doesn't exist
//     mkdir("user_files", 0777);
    
//     int s;
//     struct sockaddr_in server_addr;
//     socklen_t addr_len = sizeof(struct sockaddr_in);

//     s = socket(AF_INET, SOCK_STREAM, 0);
//     if (s < 0) {
//         perror("Socket creation failed");
//         return 1;
//     }

//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(PORT);
//     server_addr.sin_addr.s_addr = INADDR_ANY;

//     if (bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         return 1;
//     }

//     if (listen(s, 5) < 0) {
//         perror("Listen failed");
//         return 1;
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         int c = accept(s, (struct sockaddr *)&server_addr, &addr_len);
//         if (c < 0) {
//             perror("Accept failed");
//             continue;
//         }
//         handle_request(c);
//     }
    
//     close(s);
//     return 0;
// }



// // Function to handle a single client request.
// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
    
//     // Defensive check to prevent crash from corrupted requests.
//     if (!request_string || strlen(request_string) < 5) {
//         if (request_string) {
//             free(request_string);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char session_id[33] = "";
//     const char* cookie_header = strstr(request_string, "Cookie:");
//     if (cookie_header) {
//         const char* session_cookie_start = strstr(cookie_header, "session_id=");
//         if (session_cookie_start) {
//             session_cookie_start += strlen("session_id=");
//             const char* end_semicolon = strchr(session_cookie_start, ';');
//             const char* end_newline = strchr(session_cookie_start, '\r');
//             const char* end = NULL;

//             if (end_semicolon && end_newline) {
//                 end = (end_semicolon < end_newline) ? end_semicolon : end_newline;
//             } else if (end_semicolon) {
//                 end = end_semicolon;
//             } else if (end_newline) {
//                 end = end_newline;
//             }

//             size_t len = (end != NULL) ? (size_t)(end - session_cookie_start) : strlen(session_cookie_start);
//             if (len >= sizeof(session_id)) {
//                 len = sizeof(session_id) - 1;
//             }
//             strncpy(session_id, session_cookie_start, len);
//             session_id[len] = '\0';
//         }
//     }

//     char* username = get_username_from_session(session_id);
//     printf("Debug: session_id = '%s', username = '%s'\n", session_id, username ? username : "NULL");

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/logout") == 0) {
//         delete_session(session_id);
//         http_send_redirect_with_cookie(client_socket, "/login.html", "deleted");
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else if (strncmp(req->url, "/user_files/", 12) == 0) {
//         // Serve requested file from user_files
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);
//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
            
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     if (username) {
//         free(username);
//     }
//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" and "sessions" directory if it doesn't exist.
//     mkdir("user_files", 0777);
//     mkdir("sessions", 0700);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         // Fork a new process to handle the client request
//         pid_t pid = fork();
//         if (pid < 0) {
//             // Fork failed
//             perror("fork() failed");
//             close(client_socket);
//             continue;
//         } else if (pid == 0) {
//             // This is the child process
//             close(server_socket); // Child doesn't need the listening socket
//             handle_request(client_socket);
//             exit(0); // Exit the child process after handling the request
//         } else {
//             // This is the parent process
//             close(client_socket); // Parent doesn't need the client socket
//             waitpid(pid, NULL, WNOHANG); // Clean up child processes
//         }
//     }

//     close(server_socket);
//     return 0;
// }



// // Function to handle a single client request.
// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
    
//     // Defensive check to prevent crash from corrupted requests.
//     if (!request_string || strlen(request_string) < 5) {
//         if (request_string) {
//             free(request_string);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char session_id[33] = "";
//     const char* cookie_header = strstr(request_string, "Cookie:");
//     if (cookie_header) {
//         const char* session_cookie_start = strstr(cookie_header, "session_id=");
//         if (session_cookie_start) {
//             session_cookie_start += strlen("session_id=");
//             const char* end_semicolon = strchr(session_cookie_start, ';');
//             const char* end_newline = strchr(session_cookie_start, '\r');
//             const char* end = NULL;

//             if (end_semicolon && end_newline) {
//                 end = (end_semicolon < end_newline) ? end_semicolon : end_newline;
//             } else if (end_semicolon) {
//                 end = end_semicolon;
//             } else if (end_newline) {
//                 end = end_newline;
//             }

//             size_t len = (end != NULL) ? (size_t)(end - session_cookie_start) : strlen(session_cookie_start);
//             if (len >= sizeof(session_id)) {
//                 len = sizeof(session_id) - 1;
//             }
//             strncpy(session_id, session_cookie_start, len);
//             session_id[len] = '\0';
//         }
//     }

//     char* username = get_username_from_session(session_id);
//     printf("Debug: session_id = '%s', username = '%s'\n", session_id, username ? username : "NULL");

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/logout") == 0) {
//         delete_session(session_id);
//         http_send_redirect_with_cookie(client_socket, "/login.html", "deleted");
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else if (strncmp(req->url, "/user_files/", 12) == 0) {
//         // Serve requested file from user_files
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);
//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
            
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     if (username) {
//         free(username);
//     }
//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" and "sessions" directory if it doesn't exist.
//     mkdir("user_files", 0777);
//     mkdir("sessions", 0700);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         // Fork a new process to handle the client request
//         pid_t pid = fork();
//         if (pid < 0) {
//             // Fork failed
//             perror("fork() failed");
//             close(client_socket);
//             continue;
//         } else if (pid == 0) {
//             // This is the child process
//             close(server_socket); // Child doesn't need the listening socket
//             handle_request(client_socket);
//             exit(0); // Exit the child process after handling the request
//         } else {
//             // This is the parent process
//             close(client_socket); // Parent doesn't need the client socket
//             waitpid(pid, NULL, WNOHANG); // Clean up child processes
//         }
//     }

//     close(server_socket);
//     return 0;
// }


//working uploading files not serving
// Function to handle a single client request.
// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
//     if (!request_string) {
//         if (strlen(error_msg) > 0) {
//             http_send_response(client_socket, 500, "text/plain", error_msg, strlen(error_msg));
//         } else {
//             http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char session_id[33] = "";
//     const char* cookie_header = strstr(request_string, "Cookie:");
//     if (cookie_header) {
//         const char* session_cookie_start = strstr(cookie_header, "session_id=");
//         if (session_cookie_start) {
//             session_cookie_start += strlen("session_id=");
//             const char* end_semicolon = strchr(session_cookie_start, ';');
//             const char* end_newline = strchr(session_cookie_start, '\r');
//             const char* end = NULL;

//             if (end_semicolon && end_newline) {
//                 end = (end_semicolon < end_newline) ? end_semicolon : end_newline;
//             } else if (end_semicolon) {
//                 end = end_semicolon;
//             } else if (end_newline) {
//                 end = end_newline;
//             }

//             size_t len = (end != NULL) ? (size_t)(end - session_cookie_start) : strlen(session_cookie_start);
//             if (len >= sizeof(session_id)) {
//                 len = sizeof(session_id) - 1;
//             }
//             strncpy(session_id, session_cookie_start, len);
//             session_id[len] = '\0';
//         }
//     }

//     char* username = get_username_from_session(session_id);
//     printf("Debug: session_id = '%s', username = '%s'\n", session_id, username ? username : "NULL");

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
            
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" directory if it doesn't exist.
//     mkdir("user_files", 0777);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         handle_request(client_socket);
//     }

//     close(server_socket);
//     return 0;
// }


// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
//     if (!request_string) {
//         if (strlen(error_msg) > 0) {
//             http_send_response(client_socket, 500, "text/plain", error_msg, strlen(error_msg));
//         } else {
//             http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char session_id[33] = "";
//     char* cookie_header = strstr(request_string, "Cookie:");
//     if (cookie_header) {
//         cookie_header += strlen("Cookie: ");
//         char* token = strtok(cookie_header, "; ");
//         while(token != NULL) {
//             if (strncmp(token, "session_id=", strlen("session_id=")) == 0) {
//                 const char* start = token + strlen("session_id=");
//                 strncpy(session_id, start, sizeof(session_id) - 1);
//                 session_id[sizeof(session_id) - 1] = '\0';
//                 break;
//             }
//             token = strtok(NULL, "; ");
//         }
//     }

//     char* username = get_username_from_session(session_id);
//     printf("Debug: session_id = '%s', username = '%s'\n", session_id, username ? username : "NULL");

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
            
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// // Function to handle a single client request.
// // void handle_request(int client_socket) {
// //     char* request_string = read_full_request(client_socket);
// //     if (!request_string) {
// //         if (strlen(error_msg) > 0) {
// //             http_send_response(client_socket, 500, "text/plain", error_msg, strlen(error_msg));
// //         } else {
// //             http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
// //         }
// //         close(client_socket);
// //         return;
// //     }

// //     httpreq* req = parse_http(request_string);
// //     if (!req) {
// //         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
// //         close(client_socket);
// //         free(request_string);
// //         return;
// //     }

// //     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

// //     // Get session ID from cookies
// //     char session_id[33] = "";
// //     const char *cookie_header = strstr(request_string, "Cookie:");
// //     if (cookie_header) {
// //         const char *session_cookie_start = strstr(cookie_header, "session_id=");
// //         if (session_cookie_start) {
// //             session_cookie_start += strlen("session_id=");
// //             const char *session_cookie_end = strchr(session_cookie_start, ';');
// //             size_t id_len = (session_cookie_end != NULL) ? (size_t)(session_cookie_end - session_cookie_start) : strlen(session_cookie_start);
// //             if (id_len < sizeof(session_id)) {
// //                 strncpy(session_id, session_cookie_start, id_len);
// //                 session_id[id_len] = '\0';
// //             }
// //         }
// //     }

// //     char* username = get_username_from_session(session_id);
// //     printf("Debug: session_id = '%s', username = '%s'\n", session_id, username ? username : "NULL");

// //     // --- Routing logic ---
// //     if (strcmp(req->url, "/") == 0) {
// //         if (username) {
// //             http_send_dashboard(client_socket, username);
// //         } else {
// //             File* file = fileread("index.html");
// //             if (file) {
// //                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
// //                 free(file->filename);
// //                 free(file->fc);
// //                 free(file);
// //             } else {
// //                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
// //             }
// //         }
// //     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
// //         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
// //         struct FormData user_data = parse_user_data(body_data);
// //         if (register_user(user_data.username, user_data.password)) {
// //             http_send_redirect(client_socket, "/");
// //         } else {
// //             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
// //         }
// //     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
// //         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
// //         struct FormData user_data = parse_user_data(body_data);
// //         if (authenticate_user(user_data.username, user_data.password)) {
// //             char* new_session_id = create_session(user_data.username);
// //             if (new_session_id) {
// //                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
// //                 free(new_session_id);
// //             } else {
// //                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
// //             }
// //         } else {
// //             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
// //         }
// //     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
// //         if (username) {
// //             if (http_handle_upload(request_string, username)) {
// //                 http_send_redirect(client_socket, "/");
// //             } else {
// //                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
// //             }
// //         } else {
// //             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
// //         }
// //     } else {
// //         // Serve requested file if it exists
// //         char path_buffer[256];
// //         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
// //         File* file = fileread(path_buffer);

// //         if (file) {
// //             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
// //             free(file->filename);
// //             free(file->fc);
// //             free(file);
// //         } else {
// //             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
// //         }
// //     }

// //     free(req);
// //     free(request_string);
// //     close(client_socket);
// // }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" directory if it doesn't exist.
//     mkdir("user_files", 0777);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         handle_request(client_socket);
//     }

//     close(server_socket);
//     return 0;
// }




// // Function to handle a single client request.
// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
//     if (!request_string) {
//         if (strlen(error_msg) > 0) {
//             http_send_response(client_socket, 500, "text/plain", error_msg, strlen(error_msg));
//         } else {
//             http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char* session_cookie = strstr(request_string, "Cookie: session_id=");
//     char session_id[33] = "";
//     if (session_cookie) {
//         session_cookie += strlen("Cookie: session_id=");
//         char* end = strchr(session_cookie, ';');
//         if (end) {
//             strncpy(session_id, session_cookie, end - session_cookie);
//             session_id[end - session_cookie] = '\0';
//         } else {
//             strncpy(session_id, session_cookie, sizeof(session_id) - 1);
//             session_id[sizeof(session_id) - 1] = '\0';
//         }
//     }

//     char* username = get_username_from_session(session_id);

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" directory if it doesn't exist.
//     mkdir("user_files", 0777);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         handle_request(client_socket);
//     }

//     close(server_socket);
//     return 0;
// }

// over all working  down code but error is in login
// #define PORT 8080
// #define MAX_CONNECTIONS 10

// char error_msg[256];

// // Function to handle a single client request.
// void handle_request(int client_socket) {
//     char* request_string = read_full_request(client_socket);
//     if (!request_string) {
//         if (strlen(error_msg) > 0) {
//             http_send_response(client_socket, 500, "text/plain", error_msg, strlen(error_msg));
//         } else {
//             http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
//         }
//         close(client_socket);
//         return;
//     }

//     httpreq* req = parse_http(request_string);
//     if (!req) {
//         http_send_response(client_socket, 400, "text/plain", "Bad Request", 11);
//         close(client_socket);
//         free(request_string);
//         return;
//     }

//     printf("Received request: Method=%s, URL=%s\n", req->method, req->url);

//     // Get session ID from cookies
//     char* session_cookie = strstr(request_string, "Cookie: session_id=");
//     char session_id[33] = "";
//     if (session_cookie) {
//         session_cookie += strlen("Cookie: session_id=");
//         char* end = strchr(session_cookie, ';');
//         if (end) {
//             strncpy(session_id, session_cookie, end - session_cookie);
//             session_id[end - session_cookie] = '\0';
//         } else {
//             strncpy(session_id, session_cookie, sizeof(session_id) - 1);
//             session_id[sizeof(session_id) - 1] = '\0';
//         }
//     }

//     char* username = get_username_from_session(session_id);

//     // --- Routing logic ---
//     if (strcmp(req->url, "/") == 0) {
//         if (username) {
//             http_send_dashboard(client_socket, username);
//         } else {
//             File* file = fileread("index.html");
//             if (file) {
//                 http_send_response(client_socket, 200, get_content_type("index.html"), file->fc, file->size);
//                 free(file->filename);
//                 free(file->fc);
//                 free(file);
//             } else {
//                 http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//             }
//         }
//     } else if (strcmp(req->url, "/register") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (register_user(user_data.username, user_data.password)) {
//             http_send_redirect(client_socket, "/");
//         } else {
//             http_send_response(client_socket, 400, "text/html", "User already exists. <a href='/'>Go back</a>.", 46);
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char* body_data = strstr(request_string, "\r\n\r\n") + 4;
//         struct FormData user_data = parse_user_data(body_data);
//         if (authenticate_user(user_data.username, user_data.password)) {
//             char* new_session_id = create_session(user_data.username);
//             if (new_session_id) {
//                 http_send_redirect_with_cookie(client_socket, "/", new_session_id);
//                 free(new_session_id);
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "Session creation failed.", 24);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Authentication failed. <a href='/'>Go back</a>.", 47);
//         }
//     } else if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (username) {
//             if (http_handle_upload(request_string, username)) {
//                 http_send_redirect(client_socket, "/");
//             } else {
//                 http_send_response(client_socket, 500, "text/plain", "File upload failed.", 19);
//             }
//         } else {
//             http_send_response(client_socket, 401, "text/html", "Unauthorized. <a href='/'>Go to login</a>.", 40);
//         }
//     } else {
//         // Serve requested file if it exists
//         char path_buffer[256];
//         snprintf(path_buffer, sizeof(path_buffer), ".%s", req->url);
//         File* file = fileread(path_buffer);

//         if (file) {
//             http_send_response(client_socket, 200, get_content_type(file->filename), file->fc, file->size);
//             free(file->filename);
//             free(file->fc);
//             free(file);
//         } else {
//             http_send_response(client_socket, 404, "text/plain", "404 Not Found", 13);
//         }
//     }

//     free(req);
//     free(request_string);
//     close(client_socket);
// }

// int main() {
//     int server_socket, client_socket;
//     struct sockaddr_in server_addr, client_addr;
//     socklen_t client_addr_len = sizeof(client_addr);

//     // Create the "user_files" directory if it doesn't exist.
//     mkdir("user_files", 0777);

//     // Create server socket
//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket < 0) {
//         perror("Socket creation failed");
//         exit(EXIT_FAILURE);
//     }

//     // Prepare the server address structure
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = INADDR_ANY;
//     server_addr.sin_port = htons(PORT);

//     // Bind the socket to the specified address and port
//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         perror("Bind failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     // Listen for incoming connections
//     if (listen(server_socket, MAX_CONNECTIONS) < 0) {
//         perror("Listen failed");
//         close(server_socket);
//         exit(EXIT_FAILURE);
//     }

//     printf("Server listening on port %d...\n", PORT);

//     while (1) {
//         client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
//         if (client_socket < 0) {
//             perror("Accept failed");
//             continue;
//         }

//         handle_request(client_socket);
//     }

//     close(server_socket);
//     return 0;
// }

// #define PORT 8080
// #define LISTENADDRESS "0.0.0.0"

// // Global error message buffer.
// // Note: In a real-world, multi-threaded server, this would be unsafe.
// // For a multi-process server using fork(), each process gets its own copy, so it's safe.
// char error_msg[256];

// // Function signature for redirect with cookie, assuming it's in http_handler.h
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id);

// /**
//  * Handles an incoming HTTP request from a client.
//  * This function consolidates all request routing logic.
//  * @param c The client socket file descriptor.
//  */
// void handle_request(int c) {
//     char *request = read_full_request(c);
//     if (!request) {
//         http_send_response(c, 400, "text/plain", "Bad Request", 11);
//         return;
//     }

//     // FIX: parse_http takes the request string, not the socket.
//     httpreq *req = parse_http(request);
//     if (!req) {
//         http_send_response(c, 400, "text/plain", "Bad Request", 11);
//         free(request);
//         return;
//     }

//     // Authentication and session management
//     // FIX: Corrected typo from 'Cookies' to 'Cookie'
//     char *cookie_header = strstr(request, "Cookie: ");
//     char *session_id = NULL;
//     if (cookie_header) {
//         session_id = strstr(cookie_header, "session_id=");
//         if (session_id) {
//             session_id += strlen("session_id=");
//             // FIX: Using strchr is more efficient for finding a single character.
//             char *session_end = strchr(session_id, ';');
//             if (session_end) {
//                 *session_end = '\0';
//             }
//         }
//     }
//     char *username = NULL;
//     if (session_id) {
//         username = get_username_from_session(session_id);
//     }

//     // Request routing
//     if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
//         if (!username) {
//             http_send_redirect(c, "/login.html");
//         } else {
//             if (http_handle_upload(request, username)) {
//                 http_send_redirect(c, "/dashboard");
//             } else {
//                 http_send_response(c, 500, "text/plain", "File upload failed.", 19);
//             }
//         }
//     } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
//         char *body = strstr(request, "\r\n\r\n");
//         if (body) {
//             body += 4;
//             char user[256], pass[256];
//             char *user_part = strstr(body, "username=");
//             if (user_part) {
//                 user_part += strlen("username=");
//                 char *pass_part = strstr(user_part, "&password=");
//                 if (pass_part) {
//                     // FIX: Changed strncmp to strncpy for copying the string.
//                     strncpy(user, user_part, pass_part - user_part);
//                     user[pass_part - user_part] = '\0';
//                     pass_part += strlen("&password=");
//                     strncpy(pass, pass_part, sizeof(pass) - 1); // Use sizeof(pass) - 1 for safety
//                     pass[sizeof(pass) - 1] = '\0';

//                     urldecode(user, user);
//                     urldecode(pass, pass);

//                     if (authenticate_user(user, pass)) {
//                         char *new_session_id = create_session(user);
//                         if (new_session_id) {
//                             // FIX: Correctly send a redirect with a cookie after successful login.
//                             http_send_redirect_with_cookie(c, "/dashboard", new_session_id);
//                             free(new_session_id);
//                         } else {
//                             http_send_response(c, 500, "text/plain", "Session creation failed", 23);
//                         }
//                     } else {
//                         http_send_response(c, 401, "text/plain", "Invalid credentials", 19);
//                     }
//                 } else {
//                     http_send_response(c, 400, "text/plain", "Bad Request", 11);
//                 }
//             } else {
//                 http_send_response(c, 400, "text/plain", "Bad Request", 11);
//             }
//         } else {
//             http_send_response(c, 400, "text/plain", "Bad Request", 11);
//         }
//     } else if (strcmp(req->url, "/dashboard") == 0) {
//         if (!username) {
//             http_send_redirect(c, "/login.html");
//         } else {
//             http_send_dashboard(c, username);
//         }
//     } else if (strncmp(req->url, "/user/", 6) == 0) {
//         char *path = req->url + 6;
//         char *user_end = strchr(path, '/');
//         if (user_end) {
//             *user_end = '\0';
//             // FIX: The file_path variable must be a pointer.
//             char *file_path = user_end + 1;

//             // Check if the user is authorized to view the file
//             if (username && strcmp(path, username) == 0) {
//                 char full_path[256];
//                 snprintf(full_path, sizeof(full_path), "user_files/%s/%s", username, file_path);
//                 File *f = fileread(full_path);
//                 if (f) {
//                     const char *content_type = get_content_type(f->filename);
//                     http_send_response(c, 200, content_type, f->fc, f->size);
//                     free(f->fc);
//                     free(f);
//                 } else {
//                     http_send_response(c, 404, "text/plain", "File not found.", 15);
//                 }
//             } else {
//                 http_send_response(c, 403, "text/plain", "Forbidden", 9);
//             }
//         } else {
//             http_send_response(c, 404, "text/plain", "Not Found", 9);
//         }
//     } else {
//         // Serve static files
//         char path[256];
//         if (strcmp(req->url, "/") == 0) {
//             strcpy(path, "public/login.html");
//         } else {
//             snprintf(path, sizeof(path), "public%s", req->url);
//         }

//         File *f = fileread(path);
//         if (f) {
//             const char *content_type = get_content_type(f->filename);
//             http_send_response(c, 200, content_type, f->fc, f->size);
//             free(f->fc);
//             free(f);
//         } else {
//             http_send_response(c, 404, "text/plain", "File not found.", 15);
//         }
//     }

//     // Cleanup
//     free(req);
//     free(request);
// }

// int main(int argc, char **argv) {
//     int s, c;
//     struct sockaddr_in sa, ca;
//     socklen_t ca_len;

//     // Create a socket
//     s = socket(AF_INET, SOCK_STREAM, 0);
//     if (s < 0) {
//         perror("socket() failed");
//         exit(1);
//     }

//     // Bind to the port
//     sa.sin_family = AF_INET;
//     sa.sin_port = htons(PORT);
//     sa.sin_addr.s_addr = htonl(INADDR_ANY);

//     if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
//         perror("bind() failed");
//         close(s);
//         exit(1);
//     }

//     // Listen for connections
//     if (listen(s, 5) < 0) {
//         perror("listen() failed");
//         close(s);
//         exit(1);
//     }
    
//     printf("Server listening on port %d...\n", PORT);

//     // Main server loop
//     for (;;) {
//         ca_len = sizeof(ca);
//         c = accept(s, (struct sockaddr *)&ca, &ca_len);
//         if (c < 0) {
//             perror("accept() failed");
//             continue;
//         }

//         // Use fork() to create a new process for each connection.
//         pid_t pid = fork();
//         if (pid == -1) {
//             perror("fork() failed");
//             close(c);
//             continue;
//         }
        
//         if (pid == 0) { // This is the child process.
//             printf("New connection accepted. Handling in child process %d.\n", getpid());
//             close(s); // Child process does not need the server socket.
//             handle_request(c);
//             exit(0); // Terminate the child process after handling the request.
//         } else { // This is the parent process.
//             close(c); // Parent process does not need the client socket.
//         }
//     }

//     close(s);
//     return 0;
// }
