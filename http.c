// //step 1 seting server
// // parsing http requests

// This program is a simple HTTP server demonstrating how to handle both GET and POST requests.
// It includes fixes for race conditions, file handling, and proper POST body parsing.

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

#define LISTENADDRESS "0.0.0.0"
#define MAX_REQUEST_SIZE 4096 // A reasonable maximum for the entire request
#define MAX_USERNAME_LEN 65
#define MAX_PASSWORD_LEN 65
#define HASH_LEN 65

struct sHttpreq {
    char method[8];
    char url[128];
};
typedef struct sHttpreq httpreq;

struct sFile {
    char filename[64];
    char *fc; // file content (dynamically allocated)
    int size;
};
typedef struct sFile File;

// A struct to hold the parsed form data.
struct FormData {
    
    char name[MAX_USERNAME_LEN];
    char message[512];
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN]
};

// Global error message buffer.
// Note: In a real-world, multi-threaded server, this would be unsafe.
// For a multi-process server using fork(), each process gets its own copy, so it's safe.
char error_msg[256];

/**
 * A basic, non-cryptographic password hashing function for demonstration purposes.
 * In a real-world application, you would use a dedicated library like OpenSSL
 * with a strong hashing algorithm and salt.
 * This function just takes the first 64 characters of the password and converts it
 * to a simple representation to illustrate the concept.
 *
 * @param password The password string to hash.
 * @param output The buffer to store the hash. Must be at least HASH_LEN long.
 */
void hash_password(const char* password, char* output) {
    // This is a placeholder. A real implementation would use a library like OpenSSL.
    // For now, we'll just copy the password (for illustration) or a simple hash.
    strncpy(output, password, HASH_LEN - 1);
    output[HASH_LEN - 1] = '\0';
}


// Helper function to decode URL-encoded characters.
// It converts characters like %20 to spaces.
void urldecode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%') {
            if (sscanf(src + 1, "%2hhx", &a) == 1) {
                *dst++ = a;
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * Parses URL-encoded form data, specifically for username and password.
 * This is a simplified version for this step.
 * @param body_data The raw URL-encoded string.
 * @return A new FormData struct with parsed data.
 */
struct FormData parse_user_data(char *body_data){
struct FormData data;

memset(&data,0, sizeof(data));

char* username_start = strstr(body_data,"username=");
char* password_start = strstr(body_data,"password=");

if(username_start){
char *username_end;
username_start += strlen("username=");
username_end = strchr(username_start,'&');

if(username_end){
size_t username_len = username_end - username_start;
strncpy(data.username,username_start , username_len > sizeof(data.username)-1 ? sizeof(data.username)-1: username_len);
data.username[username_len > sizeof(data.username)-1 ? sizeof(data.username)-1:username_len] ='\0';
}else{
    strncpy(data.username, username_start,sizeof(data.username)-1);
    data.username[sizeof(data.username)-1] ='/0';
}
}

if(password_start){
 char *password_end; 
 password_start += strlen("password=");
 password_end = strch(password_end,'&');
 if(password_end){

size_t password_len = password_end - password_start;

strncpy(data.password, password_start, password_len > sizeof(data.password)-1 ? sizeof(data.password)-1 : password_len);
data.password[password_len > sizeof(password_start)-1 ? sizeof(password_start)-1 : password_len]='/0';

 }  else{
   strncpy(data.password , password_start, sizeof(password_start)-1);
   data.password[sizeof(data.password)-1] = '/0';
 }
}

urldecode(username_start,data.username);
urldecode(password_start,data.password);
return data;
}


/**
 * Checks if a username already exists in the users.txt file.
 * @param username The username to check.
 * @return 1 if the user exists, 0 otherwise.
 */
int user_exists(const char* username) {
    FILE* fp = fopen("users.txt", "r");
    if (!fp) {
        perror("user_exist():File not open in \n");
        return 0; // File doesn't exist yet, so no users exist.
    }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char stored_username[MAX_USERNAME_LEN];
        char* colon = strchr(line, ':');
        if (colon) {
            size_t len = colon - line;
            if (len < MAX_USERNAME_LEN) {
                strncpy(stored_username, line, len);
                stored_username[len] = '\0';
                if (strcmp(stored_username, username) == 0) {
                    fclose(fp);
                    return 1;
                }
            }
        }
    }
    fclose(fp);
    return 0;
}

/**
 * Registers a new user by appending their username and hashed password to users.txt.
 * @param username The new user's username.
 * @param password The new user's password (will be hashed).
 * @return 1 on success, 0 on failure.
 */
int register_user(const char* username, const char* password) {
    if (user_exists(username)) {
        return 0; // User already exists
    }

    FILE* fp = fopen("users.txt", "a");
    if (!fp) {
        perror("Error opening users.txt for writing");
        return 0;
    }
    
    char hashed_password[HASH_LEN];
    hash_password(password, hashed_password);
    
    fprintf(fp, "%s:%s\n", username, hashed_password);
    fclose(fp);
    return 1;
}

/**
 * Authenticates a user by checking their credentials against users.txt.
 * @param username The username to authenticate.
 * @param password The password to authenticate (will be hashed).
 * @return 1 on successful authentication, 0 otherwise.
 */
int authenticate_user(const char* username, const char* password) {
    FILE* fp = fopen("users.txt", "r");
    if (!fp) {
        return 0; // No user file, so no users can be authenticated.
    }
    
    char line[512];
    char hashed_password[HASH_LEN];
    hash_password(password, hashed_password);
    
    while (fgets(line, sizeof(line), fp)) {
        char stored_username[MAX_USERNAME_LEN];
        char stored_hash[HASH_LEN];
        
        char* colon = strchr(line, ':');
        if (colon) {
            size_t len = colon - line;
            if (len < MAX_USERNAME_LEN) {
                strncpy(stored_username, line, len);
                stored_username[len] = '\0';
                
                char* newline = strchr(colon + 1, '\n');
                if (newline) *newline = '\0';
                strncpy(stored_hash, colon + 1, HASH_LEN - 1);
                
                if (strcmp(stored_username, username) == 0 && strcmp(stored_hash, hashed_password) == 0) {
                    fclose(fp);
                    return 1;
                }
            }
        }
    }
    fclose(fp);
    return 0;
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
 * Parses the HTTP request header to find the method and URL.
 * @param str The full request string.
 * @return A new httpreq struct, or NULL on error.
 */
httpreq *parse_http(char *str) {
    httpreq *req;
    char *p = str;
    
    req = malloc(sizeof(httpreq));
    if (req == NULL) {
        snprintf(error_msg, sizeof(error_msg), "parse_http() error: memory allocation failed");
        return NULL;
    }
    memset(req, 0, sizeof(httpreq));

    // Read method (first word)
    char *method_end = strchr(p, ' ');
    if (!method_end) {
        free(req);
        return NULL;
    }
    int method_len = method_end - p;
    strncpy(req->method, p, method_len);
    req->method[method_len] = '\0';

    // Read URL (second word)
    p = method_end + 1;
    char *url_end = strchr(p, ' ');
    if (!url_end) {
        free(req);
        return NULL;
    }
    int url_len = url_end - p;
    strncpy(req->url, p, url_len);
    req->url[url_len] = '\0';

    return req;
}

/**
 * Reads the entire HTTP request from the client socket.
 * This is crucial for POST requests as it includes the body.
 * It reads until the connection closes or the buffer is full.
 * @param c The client socket file descriptor.
 * @return A dynamically allocated string with the full request, or NULL on error.
 */
// A more robust function to read the full HTTP request. It reads headers first,
// then uses Content-Length to read the exact body size.
char *read_full_request(int c) {
    char *request = NULL;
    size_t total_size = 0;
    ssize_t bytes_read;
    char buffer[4096];
    char *header_end;

    // Read the request until the header delimiter "\r\n\r\n" is found
    // or the request size limit is reached.
    while (1) {
        bytes_read = recv(c, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            if (request) free(request);
            return NULL;
        }

        // Reallocate memory for the request buffer
        char *temp_request = realloc(request, total_size + bytes_read + 1);
        if (!temp_request) {
            perror("realloc() failed");
            if (request) free(request);
            return NULL;
        }
        request = temp_request;
        memcpy(request + total_size, buffer, bytes_read);
        total_size += bytes_read;
        request[total_size] = '\0';

        header_end = strstr(request, "\r\n\r\n");
        if (header_end != NULL) {
            // Found the end of headers, break the loop
            break;
        }

        // Check for request size limit
        if (total_size >= MAX_REQUEST_SIZE) {
            fprintf(stderr, "Request size exceeds limit.\n");
            if (request) free(request);
            return NULL;
        }
    }
    
    // Check if the request is a POST request and has a body
    char *cl_header = strstr(request, "Content-Length: ");
    if (cl_header) {
        int content_length = atoi(cl_header + strlen("Content-Length: "));
        // Calculate the size of the body data already read with the headers
        size_t body_start_offset = (header_end - request) + 4;
        size_t current_body_size = total_size - body_start_offset;
        int remaining_bytes = content_length - current_body_size;
        
        // Read the rest of the body if not all of it was received
        if (remaining_bytes > 0) {
            char *temp_request = realloc(request, total_size + remaining_bytes + 1);
            if (!temp_request) {
                perror("realloc() failed for body");
                free(request);
                return NULL;
            }
            request = temp_request;
            bytes_read = recv(c, request + total_size, remaining_bytes, 0);
            if (bytes_read > 0) {
                total_size += bytes_read;
                request[total_size] = '\0';
            }
        }
    }

    return request;
}


/**
 * Reads the entire contents of a file into a dynamically allocated struct.
 * @param filename The path to the file to read.
 * @return A pointer to a new File struct, or NULL on error.
 */
File *fileread(char *filename) {
    int n, fd;
    File *f;
    
    f = malloc(sizeof(File));
    if (f == NULL) {
        perror("malloc() error for File struct");
        return NULL;
    }
    
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open() error");
        free(f);
        return NULL;
    }
    
    strncpy(f->filename, filename, sizeof(f->filename) - 1);
    f->filename[sizeof(f->filename) - 1] = '\0';

    f->fc = malloc(1); // Start with a small buffer.
    f->size = 0;

    char temp_buf[512];
    while ((n = read(fd, temp_buf, sizeof(temp_buf))) > 0) {
        void *realloc_ptr = realloc(f->fc, f->size + n);
        if (realloc_ptr == NULL) {
            perror("realloc() error");
            close(fd);
            free(f->fc);
            free(f);
            return NULL;
        }
        f->fc = realloc_ptr;
        memcpy(f->fc + f->size, temp_buf, n);
        f->size += n;
    }

    if (n < 0) {
        perror("read() error");
        close(fd);
        free(f->fc);
        free(f);
        return NULL;
    }

    close(fd);
    f->fc[f->size] = '\0'; // Null-terminate
    
    return f;
}


/**
 * Sends the HTTP status line, headers, and data to the client.
 * @param c The client socket file descriptor.
 * @param code The HTTP status code.
 * @param contentType The Content-Type header value.
 * @param data The response body.
 * @param data_length The size of the response body in bytes.
 */
void http_send_response(int c, int code, const char *contentType, const char *data, int data_length) {
    char header_buf[1024];
    int n;

    snprintf(header_buf, sizeof(header_buf) - 1,
        "HTTP/1.0 %d OK\r\n"
        "Server: httpd.c\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n", // The crucial blank line
        code, contentType, data_length
    );

    n = strlen(header_buf);
    write(c, header_buf, n);
    write(c, data, data_length); 
}

/**
 * Determines the Content-Type based on a file extension.
 * @param path The file path.
 * @return A string containing the correct MIME type.
 */
const char *get_content_type(const char *path) {
    const char *extension = strrchr(path, '.');
    if (extension == NULL) {
        return "text/plain";
    }

    if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) return "text/html";
    if (strcmp(extension, ".css") == 0) return "text/css";
    if (strcmp(extension, ".js") == 0) return "application/javascript";
    if (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0) return "image/jpeg";
    if (strcmp(extension, ".png") == 0) return "image/png";
    if (strcmp(extension, ".gif") == 0) return "image/gif";
    if (strcmp(extension, ".mp4") == 0) return "video/mp4";
    
    return "text/plain";
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
            printf("Received POST body: %s\n", body_data);

            // 1. Parse the raw POST data into a structured format
            struct FormData form_data = parse_user_data(body_data);
     
            // 2. Open the file in append mode ("a") and format the output
            FILE *fp = fopen("form_data.txt", "a");
            if (fp != NULL) {
                time_t now = time(NULL);
                struct tm time_info; // New local struct for time info
                struct tm *t = localtime_r(&now, &time_info); // Use thread-safe version

                if (t != NULL) { // Check for a valid return from localtime_r
                    fprintf(fp, "[%d-%02d-%02d %02d:%02d:%02d] Name: %s, Message: %s\n",
                            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                            t->tm_hour, t->tm_min, t->tm_sec,
                            form_data.name, form_data.message);
                } else {
                    fprintf(fp, "[Time Error] Name: %s, Message: %s\n",
                            form_data.name, form_data.message);
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
            f = fileread("./success.html");
            res = "<h2>Data Submitted Successfully!</h2><p>Check the form_data.txt file on the server.</p>";
            // http_send_response(c, 200, "text/html", res, strlen(res));
            http_send_response(c, 200, "text/html", f->fc, f->size);
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




