// //step 1 seting server
// // parsing http requests

// This program is a simple HTTP server demonstrating how to handle both GET and POST requests.
// It includes fixes for race conditions, file handling, and proper POST body parsing.
#include "http_handler.h"
#include "auth.h"

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
#include <sys/stat.h> // For stat()
#include <netinet/in.h> // Sockets library for internet addresses
#include <fcntl.h>      // File control options





#define LISTENADDRESS "0.0.0.0"


// Global error message buffer.
// Note: In a real-world, multi-threaded server, this would be unsafe.
// For a multi-process server using fork(), each process gets its own copy, so it's safe.
char error_msg[256];


// Function signature for redirect with cookie, assuming it's in http_handler.h
// Function signature for redirect with cookie, assuming it's in http_handler.h
void http_send_redirect_with_cookie(int c, const char *url, const char *session_id);

/**
 * Handles an incoming HTTP request from a client.
 * This is the main request handling function that performs parsing,
 * authentication, and routing based on the URL and method.
 * @param c The client socket file descriptor.
 */
void handle_request(int c) {
    char *request = read_full_request(c);
    if (!request) {
        http_send_response(c, 400, "text/plain", "Bad Request", 11);
        return;
    }

    httpreq *req = parse_http(request);
    if (!req) {
        http_send_response(c, 400, "text/plain", "Bad Request", 11);
        free(request);
        return;
    }

    // Authentication and session management
    char *cookie_header = strstr(request, "Cookie: "); // Correctly looking for "Cookie:"
    char *session_id = NULL;
    if (cookie_header) {
        session_id = strstr(cookie_header, "session_id=");
        if (session_id) {
            session_id += strlen("session_id=");
            char *session_end = strchr(session_id, ';');
            if (session_end) {
                *session_end = '\0';
            }
        }
    }
    char *username = NULL;
    if (session_id) {
        username = get_username_from_session(session_id);
    }
    
    // Request routing
    if (strcmp(req->url, "/upload") == 0 && strcmp(req->method, "POST") == 0) {
        if (!username) {
            http_send_redirect(c, "/login.html");
        } else {
            if (http_handle_upload(request, username)) {
                http_send_redirect(c, "/dashboard");
            } else {
                http_send_response(c, 500, "text/plain", "File upload failed.", 19);
            }
        }
    } else if (strcmp(req->url, "/login") == 0 && strcmp(req->method, "POST") == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            char user[256], pass[256];
            char *user_part = strstr(body, "username="); // Corrected string search
            if (user_part) {
                user_part += strlen("username=");
                char *pass_part = strstr(user_part, "&password=");
                if (pass_part) {
                    strncpy(user, user_part, pass_part - user_part);
                    user[pass_part - user_part] = '\0';
                    pass_part += strlen("&password=");
                    strncpy(pass, pass_part, sizeof(pass));
                    pass[sizeof(pass) - 1] = '\0';
                    
                    urldecode(user, user);
                    urldecode(pass, pass);
                    
                    if (authenticate_user(user, pass)) {
                        char *new_session_id = create_session(user);
                        if (new_session_id) {
                            http_send_redirect_with_cookie(c, "/dashboard", new_session_id);
                            free(new_session_id);
                        } else {
                            http_send_response(c, 500, "text/plain", "Session creation failed", 23);
                        }
                    } else {
                        http_send_response(c, 401, "text/plain", "Invalid credentials", 19);
                    }
                } else {
                    http_send_response(c, 400, "text/plain", "Bad Request", 11);
                }
            } else {
                http_send_response(c, 400, "text/plain", "Bad Request", 11);
            }
        } else {
            http_send_response(c, 400, "text/plain", "Bad Request", 11);
        }
    } else if (strcmp(req->url, "/dashboard") == 0) {
        if (!username) {
            http_send_redirect(c, "/login.html");
        } else {
            http_send_dashboard(c, username);
        }
    } else if (strncmp(req->url, "/user/", 6) == 0) {
        char *path = req->url + 6;
        char *user_end = strchr(path, '/');
        if (user_end) {
            *user_end = '\0';
            char *file_path = user_end + 1; // Corrected to be a pointer
            
            // Check if the user is authorized to view the file
            if (username && strcmp(path, username) == 0) {
                char full_path[256];
                snprintf(full_path, sizeof(full_path), "user_files/%s/%s", username, file_path);
                File *f = fileread(full_path);
                if (f) {
                    const char *content_type = get_content_type(f->filename);
                    http_send_response(c, 200, content_type, f->fc, f->size);
                    free(f->fc);
                    free(f);
                } else {
                    http_send_response(c, 404, "text/plain", "File not found.", 15);
                }
            } else {
                http_send_response(c, 403, "text/plain", "Forbidden", 9);
            }
        } else {
            http_send_response(c, 404, "text/plain", "Not Found", 9);
        }

    } else {
        // Serve static files
        char path[256];
        if (strcmp(req->url, "/") == 0) {
            strcpy(path, "public/login.html");
        } else {
            snprintf(path, sizeof(path), "public%s", req->url);
        }
        
        File *f = fileread(path);
        if (f) {
            const char *content_type = get_content_type(f->filename);
            http_send_response(c, 200, content_type, f->fc, f->size);
            free(f->fc);
            free(f);
        } else {
            http_send_response(c, 404, "text/plain", "File not found.", 15);
        }
    }
    
    // Cleanup
    free(req);
    free(request);
}
/**
 * Initializes the server socket.
 * @param portno The port number to listen on.
 * @return The server socket file descriptor, or 0 on error.
 */
int serv_init(int portno) {
    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(error_msg, sizeof(error_msg), "Socket() error: %s\n", strerror(errno));
        return 0;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = inet_addr(LISTENADDRESS);

    if (bind(sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
        snprintf(error_msg, sizeof(error_msg), "Bind() error: %s\n", strerror(errno));
        close(sockfd);
        return 0;
    }

    if (listen(sockfd, 5)) {
        snprintf(error_msg, sizeof(error_msg), "Listen() error: %s\n", strerror(errno));
        close(sockfd);
        return 0;
    }

    return sockfd;
}

/**
 * Accepts a new client connection.
 * @param s The server socket file descriptor.
 * @return The client socket file descriptor, or 0 on error.
 */
int client_acpt(int s) {
    int c; // c = client fd
    struct sockaddr_in cli_addr;
    socklen_t addrlength = sizeof(cli_addr);

    memset(&cli_addr, 0, sizeof(cli_addr));
    c = accept(s, (struct sockaddr *)&cli_addr, &addrlength);

    if (c < 0) {
        snprintf(error_msg, sizeof(error_msg), "Accept() error: %s\n", strerror(errno));
        return 0;
    }
    return c;
}

/**
 * The main handler for a client connection.
 * @param c The client socket file descriptor.
 */
void cli_conn(int c) {
    httpreq *req;
    char *full_request_data;
    File *f;
    const char *res;

    // Read the entire request, including headers and potential body.
    full_request_data = read_full_request(c);

    if (!full_request_data) {
        fprintf(stderr, "Error reading request.\n");
        close(c);
        return;
    }
    
    // Parse the request headers.
    req = parse_http(full_request_data);
    if (!req) {
        fprintf(stderr, "Error parsing request: %s\n", error_msg);
        free(full_request_data);
        close(c);
        return;
    }

    //printf("Method: %s, URL: %s\n", req->method, req->url);

    if (strcmp(req->method, "GET") == 0) {
       char file_path[256]; 
        if (strcmp(req->url, "/") == 0) {
            snprintf(file_path, sizeof(file_path), "index.html");
        } else {
            snprintf(file_path, sizeof(file_path), ".%s", req->url);
        }
       
        f = fileread(file_path);
        if (!f) {
            res = "File not found";
            http_send_response(c, 404, "text/plain", res, strlen(res));
        } else {
            const char *content_type = get_content_type(file_path);
            http_send_response(c, 200, content_type, f->fc, f->size);
            free(f->fc);
            free(f);
        }
    } else if (strcmp(req->method, "POST") == 0) {
        // Correctly find the body data after the headers.
        char *header_end = strstr(full_request_data, "\r\n\r\n");
        char *body_data = NULL;
       if (header_end) {
            body_data = header_end + 4;

           if(strcmp(req->url,"/register")==0){

           // printf("Received POST body in register: %s\n", body_data);
           struct FormData user_data = parse_user_data(body_data);
           // printf("in reg user_name: %s user_pas: %s \n", user_data.username,user_data.password);
           if(strlen(user_data.username) > 0 && strlen(user_data.password) > 0){
            if(register_user(user_data.username,user_data.password)){
                res = " <p>registraition sucessfull <p> ";
                http_send_response(c, 200, "text/html", res, strlen(res));
            }else{
                res = "<h2>Registration Failed</h2><p>User already exists or server error.</p>";
                 http_send_response(c, 200, "text/html", res, strlen(res));
            }
           }else{
            res = "Bad Request: missing username and password";
            http_send_response(c,400,"text/plain",res,strlen(res));
           }
           }else if(strcmp(req->url,"/login")==0){
            // printf("Received POST body in login: %s\n", body_data);
             struct FormData user_data = parse_user_data(body_data);
             // printf("in log user_name: %s user_pas %s \n", user_data.username,user_data.password);
             if(strlen(user_data.username) > 0 && strlen(user_data.password)>0){
             if(authenticate_user(user_data.username,user_data.password)){
                 printf("Login successful. Sending dynamic dashboard for user: %s\n", user_data.username);
                        http_send_dashboard(c, user_data.username);
                // NEW: On successful login, redirect the user to the dashboard.
              //http_send_redirect(c, "/dashboard.html");
            //snprintf(file_path, sizeof(file_path), ".%s", req->url);
            //f = fileread(req->url);
            // printf("url now %s \n",req->url);
            // http_send_response(c,302,"text/html",f->fc,f->size);
                // res = "<h2> Loging Sucessful</h2> <p> Wellcom ";
                // char sucess_msg[512];
                // snprintf(sucess_msg,sizeof(sucess_msg),"%s%s</p>",res,user_data.username);
                // http_send_response(c,200,"text/html",sucess_msg,strlen(sucess_msg));
            //  struct stat buffer;
            //             if (stat("dashboard.html", &buffer) == 0) {
            //                // printf("Login successful. Redirecting to dashboard.\n");
            //                 http_send_redirect(c, "/dashboard.html");
            //             } else {
            //                 printf("Login successful, but dashboard.html not found.\n");
            //                 res = "<h2>Login Successful!</h2><p>Dashboard file not found on server.</p>";
            //                 http_send_response(c, 200, "text/html", res, strlen(res));
            //             } 
            
             }else{
                 res = "<h2>Login Failed</h2><p>Invalid username or password.</p>";
                 http_send_response(c, 200, "text/html", res,
                             strlen(res));
              }
             }else{
                res = "Bad request";
                http_send_response(c,400,"text/plain",res,strlen(res));
             }
           }else if(strcmp(req->url, "/upload") == 0){
            // This is a placeholder for session management. For now, assume a static user.
                const char *username = "testuser"; 
                if (http_handle_upload(full_request_data, username)) {
                    res = "<h2>File Upload Successful!</h2><p><a href=\"/\">Return to dashboard</a></p>";
                    http_send_response(c, 200, "text/html", res, strlen(res));
                } else {
                    res = "<h2>File Upload Failed!</h2><p>Server error.</p>";
                    http_send_response(c, 500, "text/html", res, strlen(res));
                }
           }else if(strcmp(req->url,"/form")==0){

             printf("Received POST body in form: %s\n", body_data);

            // 1. Parse the raw POST data into a structured format
            struct FormData form_data ;
            
            // 2. Open the file in append mode ("a") and format the output
            FILE *fp = fopen("form_data.txt", "a");
           char *name_start = strstr(body_data, "name=");
            char *message_start = strstr(body_data, "message=");
if(name_start){
 name_start += strlen("name=");
                    char *name_end = strchr(name_start, '&');
                    if (name_end) {
                        size_t name_len = name_end - name_start;
                        strncpy(form_data.username, name_start, name_len > sizeof(form_data.username) - 1 ? sizeof(form_data.username) - 1 : name_len);
                        form_data.username[name_len > sizeof(form_data.username) - 1 ? sizeof(form_data.username) - 1 : name_len] = '\0';
                        urldecode(form_data.username, form_data.username);
                    } else {
                        strncpy(form_data.username, name_start, sizeof(form_data.username) - 1);
                        form_data.username[sizeof(form_data.username) - 1] = '\0';
                        urldecode(form_data.username, form_data.username);
                    }
}
if(message_start){
      message_start += strlen("message=");
                    size_t message_len = strlen(message_start);
                    strncpy(form_data.message, message_start, message_len > sizeof(form_data.message) - 1 ? sizeof(form_data.message) - 1 : message_len);
                    form_data.message[message_len > sizeof(form_data.message) - 1 ? sizeof(form_data.message) - 1 : message_len] = '\0';
                    urldecode(form_data.message, form_data.message);
}
            if (fp != NULL) {
                time_t now = time(NULL);
                struct tm time_info; // New local struct for time info
                struct tm *t = localtime_r(&now, &time_info); // Use thread-safe version

                if (t != NULL) { // Check for a valid return from localtime_r
                    fprintf(fp, "[%d-%02d-%02d %02d:%02d:%02d] Name: %s, Message: %s\n",
                            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                            t->tm_hour, t->tm_min, t->tm_sec,
                            form_data.username, form_data.message);
                } else {
                    fprintf(fp, "[Time Error] Name: %s, Message: %s\n",
                            form_data.username, form_data.message);
                    perror("localtime_r failed");
                }
                fclose(fp); // CRITICAL FIX: Close the file handle
            } else {
                // This `else` block now correctly logs an error but does not send an incorrect response
                perror("fopen failed to create form_data.txt");
            }
             // The success response is now sent after all file operations,
            // regardless of whether the file was successfully opened.
            // This ensures a 200 OK response is always sent for a valid POST request.
            //f = fileread("./success.html");
            res = "<h2>Data Submitted Successfully!</h2><p>Check the form_data.txt file on the server.</p>";
            http_send_response(c, 200, "text/html", res, strlen(res));
           }else{
             //other routes 
           }

           
        } else {
            fprintf(stderr, "Error: No header end found in POST request.\n");
            res = "Bad Request";
            http_send_response(c, 400, "text/plain", res, strlen(res));
        }

    } else {
        res = "Method not supported ";
        http_send_response(c, 405, "text/plain", res, strlen(res));
    }

    // Clean up allocated memory.
    free(req);
    free(full_request_data);
    close(c);
}


int main(int argc, char *argv[]) {
    int s, nsockfd;
    char *portno;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <portno>\n", argv[0]);
        return -1;
    }
    
    portno = argv[1];
    s = serv_init(atoi(portno));
    if (!s) {
        fprintf(stderr, "Error: %s", error_msg);
        return -1;
    }

    printf("Listening on %s:%s\n", LISTENADDRESS, portno);

    while (1) {
        nsockfd = client_acpt(s);
        if (!nsockfd) {
            fprintf(stderr, "%s\n", error_msg);
            continue;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork() failed");
            close(nsockfd);
            continue;
        }

        if (pid == 0) { // This is the child process.
            printf("New connection accepted. Handling in child process %d.\n", getpid());
            close(s); // The child process doesn't need the server socket.
            cli_conn(nsockfd);
            exit(0); // Terminate the child process after handling the request.
        } else { // This is the parent process.
            // FIX: The parent should not close the client socket. The child's close() call
            // will handle the socket termination. Closing it here creates a race condition.
            // close(nsockfd);
        }
    }
    close(s);
    return 0; // The while loop prevents this from being reached.
}



// #define MAX_REQUEST_SIZE 4096 // A reasonable maximum for the entire request
// #define MAX_USERNAME_LEN 65
// #define MAX_PASSWORD_LEN 65
// #define HASH_LEN 65

// struct sHttpreq {
//     char method[8];
//     char url[128];
// };
// typedef struct sHttpreq httpreq;

// struct sFile {
//     char filename[64];
//     char *fc; // file content (dynamically allocated)
//     int size;
// };
// typedef struct sFile File;

// // A struct to hold the parsed form data.
// struct FormData {
    
//     char name[MAX_USERNAME_LEN];
//     char message[512];
//     char username[MAX_USERNAME_LEN];
//     char password[MAX_PASSWORD_LEN];
// };
