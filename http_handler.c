// // http_handler.c
// // Implements the HTTP request parsing, file reading, and response sending.

#include "http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>


#include <ctype.h> // For isxdigit()

#define MAX_HTTP_HEADER_LEN 8192

// Global buffer for error messages.
extern char error_msg[256];


/**
 * Dynamically resizes a buffer.
 */
static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
    char* new_buffer = realloc(buffer, new_size);
    if (!new_buffer) {
        free(buffer);
        snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
        return NULL;
    }
    return new_buffer;
}

/**
 * Reads the full HTTP request from a socket.
 */
char* read_full_request(int c) {
    char* request = NULL;
    size_t buffer_size = 1024;
    size_t total_received = 0;
    
    request = malloc(buffer_size);
    if (!request) {
        snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
        return NULL;
    }
    
    int bytes_received;
    while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
        request[total_received] = '\0';
        
        // Check for end of headers
        if (strstr(request, "\r\n\r\n")) {
            // Found the end of headers, now check for body
            char* content_length_str = strstr(request, "Content-Length:");
            if (content_length_str) {
                int content_length = atoi(content_length_str + strlen("Content-Length:"));
                size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
                if (total_received >= headers_length + content_length) {
                    break;
                }
            } else {
                break; // No content-length, so headers are the end
            }
        }
        
        // Resize buffer if needed
        if (total_received >= buffer_size - 1) {
            buffer_size *= 2;
            request = reallocate_buffer(request, total_received, buffer_size);
            if (!request) return NULL;
        }
    }
    
    if (bytes_received < 0) {
        free(request);
        snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
        return NULL;
    }
    
    return request;
}

/**
 * Parses a simple HTTP request and returns a httpreq struct.
 */
httpreq *parse_http(const char *request) {
    if (!request) return NULL;
    
    httpreq *req = malloc(sizeof(httpreq));
    if (!req) {
        snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
        return NULL;
    }
    memset(req, 0, sizeof(httpreq));
    
    char *request_copy = strdup(request);
    if (!request_copy) {
        free(req);
        snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
        return NULL;
    }
    
    char *line = strtok(request_copy, "\r\n");
    if (line) {
        sscanf(line, "%s %s %*s", req->method, req->url);
    }
    
    free(request_copy);
    return req;
}

/**
 * Sends an HTTP response.
 */
void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
    char header[MAX_HTTP_HEADER_LEN];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, get_status_message(status_code), content_type, body_len);
    send(c, header, strlen(header), 0);
    send(c, body, body_len, 0);
}

/**
 * Sends an HTTP redirect response.
 */
void http_send_redirect(int c, const char *url) {
    char header[MAX_HTTP_HEADER_LEN];
    snprintf(header, sizeof(header),
             "HTTP/1.1 302 Found\r\n"
             "Location: %s\r\n"
             "Connection: close\r\n"
             "\r\n", url);
    send(c, header, strlen(header), 0);
}

/**
 * Sends an HTTP redirect response with a session cookie.
 */
void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
    char header[MAX_HTTP_HEADER_LEN];
    snprintf(header, sizeof(header),
             "HTTP/1.1 302 Found\r\n"
             "Location: %s\r\n"
             "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
             "Connection: close\r\n"
             "\r\n", url, session_id);
    printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
    send(c, header, strlen(header), 0);
}

/**
 * Gets the status message for a given status code.
 */
const char* get_status_message(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

/**
 * Gets the Content-Type for a given filename.
 */
const char* get_content_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    return "application/octet-stream";
}

/**
 * URL-decodes a string in-place.
 */
void urldecode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= 'A' - 10;
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= 'A' - 10;
            else
                b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
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
 * Reads a file into a File struct.
 */
File* fileread(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == -1) {
        return NULL;
    }
    
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }
    
    File* f = malloc(sizeof(File));
    if (!f) {
        fclose(fp);
        return NULL;
    }
    
    f->size = st.st_size;
    f->fc = malloc(f->size);
    if (!f->fc) {
        free(f);
        fclose(fp);
        return NULL;
    }
    
    f->filename = strdup(filename);
    if (!f->filename) {
        free(f->fc);
        free(f);
        fclose(fp);
        return NULL;
    }
    
    size_t read_size = fread(f->fc, 1, f->size, fp);
    fclose(fp);
    if (read_size != f->size) {
        free(f->fc);
        free(f->filename);
        free(f);
        return NULL;
    }
    
    return f;
}

/**
 * Handles the file upload logic for a multipart/form-data request.
 */
int http_handle_upload(const char *request, const char* username) {
    // 1. Find the multipart boundary
    const char *boundary_start = strstr(request, "boundary=");
    if (!boundary_start) return 0;
    boundary_start += strlen("boundary=");
    char boundary[128];
    char *boundary_end = strchr(boundary_start, '\r');
    if (!boundary_end) return 0;
    size_t boundary_len = boundary_end - boundary_start;
    strncpy(boundary, boundary_start, boundary_len);
    boundary[boundary_len] = '\0';
    
    // 2. Find the start and end of the file data.
    // The file data starts after the second \r\n\r\n
    const char* part_headers_end = strstr(request, "\r\n\r\n");
    if (!part_headers_end) return 0;
    const char* data_start = part_headers_end + 4;

    // Find the closing boundary line, which includes the trailing hyphens.
    char closing_boundary_line[256];
    snprintf(closing_boundary_line, sizeof(closing_boundary_line), "\r\n--%s--", boundary);

    const char *file_end_boundary = strstr(data_start, closing_boundary_line);
    if (!file_end_boundary) return 0;

    // Calculate the length of the file content
    size_t data_len = file_end_boundary - data_start;
    
    // 3. Find the filename in the headers
    const char *file_content_start = strstr(request, "filename=\"");
    if (!file_content_start) return 0;
    file_content_start += strlen("filename=\"");
    char *file_name_end = strchr(file_content_start, '\"');
    if (!file_name_end) return 0;
    
    char file_name[256];
    size_t file_name_len = file_name_end - file_content_start;
    
    // Check for an empty filename. This prevents the "Is a directory" error.
    if (file_name_len == 0) {
        return 0; // Return failure to prevent writing to a directory
    }
    
    strncpy(file_name, file_content_start, file_name_len);
    file_name[file_name_len] = '\0';
    
    // 4. Create the user's directory if it doesn't exist
    char user_dir[256];
    snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
        perror("Error creating user directory");
        return 0;
    }
    
    // 5. Write the file content to disk
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
    FILE *fp = fopen(file_path, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return 0;
    }
    
    // Only write if there is content to write
    if (data_len > 0) {
        fwrite(data_start, 1, data_len, fp);
    }
    
    fclose(fp);
    
    return 1;
}

/**
 * Handles the file deletion logic for a POST request.
 */
int http_handle_delete_file(const char* request, const char* username) {
    // Parse the POST request body for the filename.
    const char* body_start = strstr(request, "\r\n\r\n");
    if (!body_start) return 0;
    body_start += 4;
    
    char file_name_urlencoded[256];
    if (sscanf(body_start, "filename=%s", file_name_urlencoded) != 1) {
        return 0; // Could not parse filename from body
    }
    
    char file_name[256];
    urldecode(file_name, file_name_urlencoded);
    
    // Construct the path and ensure it's within the user's directory.
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "user_files/%s/%s", username, file_name);
    
    // Simple security check: prevent directory traversal.
    if (strstr(file_path, "..")) {
        return 0;
    }

    if (remove(file_path) == 0) {
        printf("Debug: Successfully deleted file: %s\n", file_path);
        return 1;
    } else {
        perror("Error deleting file");
        return 0;
    }
}

/**
 * Generates and sends the dashboard HTML page with the user's files.
 */
void http_send_dashboard(int c, const char* username) {
    char user_dir[256];
    snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
    char file_list_html[8192] = "";
    DIR *d = opendir(user_dir);
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                // Increased buffer size to prevent truncation warning
                char item_html[2048]; 
                snprintf(item_html, sizeof(item_html),
                         "<li><a href=\"/user_files/%s/%s\">%s</a>"
                         "<form action=\"/delete_file\" method=\"post\" style=\"display:inline; margin-left: 10px;\">"
                         "<input type=\"hidden\" name=\"filename\" value=\"%s\">"
                         "<button type=\"submit\" style=\"background-color:#d9534f; color:white; border:none; padding:5px 10px; border-radius:4px; cursor:pointer;\">Delete</button>"
                         "</form>"
                         "</li>",
                         username, dir->d_name, dir->d_name, dir->d_name);
                strcat(file_list_html, item_html);
            }
        }
        closedir(d);
    } else {
        strcat(file_list_html, "<li>No files uploaded yet.</li>");
    }
    
    // Using a more robust method to generate the full HTML body
    const char *dashboard_template_part1 =
        "<!DOCTYPE html>"
        "<html><head><title>Dashboard</title><style>"
        "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
        ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
        "h1 { color: #333; }"
        "form { margin-top: 20px; }"
        "label, input { margin-bottom: 10px; }"
        "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
        "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
        "ul { list-style-type: none; padding: 0; }"
        "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
        ".logout-form { text-align: right; margin-bottom: 20px; }"
        ".logout-button { background-color: #d9534f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
        "</style></head><body>"
        "<div class=\"container\">"
        "<div class=\"logout-form\">"
        "<form action=\"/logout\" method=\"post\">"
        "<button type=\"submit\" class=\"logout-button\">Logout</button>"
        "</form>"
        "</div>"
        "<h1>Welcome, %s!</h1>"
        "<p>This is your personal dashboard. You can upload and view your files here. To update a file, simply upload a new one with the same name.</p>"
        "<h2>Upload a File</h2>"
        "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
        "<label for=\"fileToUpload\">Choose a file to upload:</label>"
        "<input type=\"file\" name=\"file_to_upload\" id=\"fileToUpload\">"
        "<input type=\"submit\" value=\"Upload\">"
        "</form>"
        "<h2>Your Files</h2>"
        "<ul>%s</ul>"
        "</div></body></html>";
    
    // Determine the required buffer size for the entire body
    size_t body_len = snprintf(NULL, 0, dashboard_template_part1, username, file_list_html) + 1;
    char *body = malloc(body_len);
    if (!body) {
        http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
        return;
    }
    
    // Write the full HTML to the new buffer
    snprintf(body, body_len, dashboard_template_part1, username, file_list_html);
    http_send_response(c, 200, "text/html", body, strlen(body));
    free(body);
}

/**
 * Generates and sends the login HTML page.
 */
void http_send_login_page(int c) {
    const char *html_content = 
        "<!DOCTYPE html>"
        "<html><head><title>Login</title><style>"
        "body { font-family: Arial, sans-serif; background-color: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }"
        ".login-container { background-color: #fff; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); width: 300px; text-align: center; }"
        "h1 { color: #333; margin-bottom: 24px; }"
        ".input-group { margin-bottom: 15px; }"
        "input[type='text'], input[type='password'] { width: calc(100% - 20px); padding: 10px; border: 1px solid #ddd; border-radius: 4px; }"
        "button { width: 100%; padding: 10px; border: none; border-radius: 4px; background-color: #007bff; color: white; font-size: 16px; cursor: pointer; }"
        "button:hover { background-color: #0056b3; }"
        "</style></head><body>"
        "<div class='login-container'>"
        "<h1>Secure Login</h1>"
        "<form action='/login' method='post'>"
        "<div class='input-group'>"
        "<input type='text' name='username' placeholder='Username' required>"
        "</div>"
        "<div class='input-group'>"
        "<input type='password' name='password' placeholder='Password' required>"
        "</div>"
        "<button type='submit'>Login</button>"
        "</form>"
        "</div>"
        "</body></html>";

    http_send_response(c, 200, "text/html", html_content, strlen(html_content));
}

/**
 * Extracts the session ID from the "Cookie" header in the HTTP request.
 */
const char* get_session_id_from_request(const char* request) {
    const char* cookie_header = strstr(request, "Cookie: ");
    if (cookie_header) {
        cookie_header += strlen("Cookie: ");
        const char* session_id_start = strstr(cookie_header, "session_id=");
        if (session_id_start) {
            session_id_start += strlen("session_id=");
            const char* session_id_end = strchr(session_id_start, ';');
            if (!session_id_end) {
                // No more cookies, session_id is the last one
                session_id_end = strchr(session_id_start, '\r');
            }
            if (session_id_end) {
                size_t id_len = session_id_end - session_id_start;
                static char session_id[33]; // UUID is 32 chars + null terminator
                if (id_len > 32) {
                    id_len = 32;
                }
                strncpy(session_id, session_id_start, id_len);
                session_id[id_len] = '\0';
                return session_id;
            }
        }
    }
    return NULL;
}

// http_handler.c
// Implements HTTP parsing, response sending, and file handling.

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     // 2. Find the start and end of the file data.
//     // The file data starts after the second \r\n\r\n
//     const char* part_headers_end = strstr(request, "\r\n\r\n");
//     if (!part_headers_end) return 0;
//     const char* data_start = part_headers_end + 4;

//     // Find the closing boundary line, which includes the trailing hyphens.
//     char closing_boundary_line[256];
//     snprintf(closing_boundary_line, sizeof(closing_boundary_line), "\r\n--%s--", boundary);

//     const char *file_end_boundary = strstr(data_start, closing_boundary_line);
//     if (!file_end_boundary) return 0;

//     // Calculate the length of the file content
//     size_t data_len = file_end_boundary - data_start;
    
//     // 3. Find the filename in the headers
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     char file_name[256];
//     size_t file_name_len = file_name_end - file_content_start;
    
//     // Check for an empty filename. This prevents the "Is a directory" error.
//     if (file_name_len == 0) {
//         return 0; // Return failure to prevent writing to a directory
//     }
    
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512];
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     // Only write if there is content to write
//     if (data_len > 0) {
//         fwrite(data_start, 1, data_len, fp);
//     }
    
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Handles the file deletion logic for a POST request.
//  */
// int http_handle_delete_file(const char* request, const char* username) {
//     // Parse the POST request body for the filename.
//     const char* body_start = strstr(request, "\r\n\r\n");
//     if (!body_start) return 0;
//     body_start += 4;
    
//     char file_name_urlencoded[256];
//     if (sscanf(body_start, "filename=%s", file_name_urlencoded) != 1) {
//         return 0; // Could not parse filename from body
//     }
    
//     char file_name[256];
//     urldecode(file_name, file_name_urlencoded);
    
//     // Construct the path and ensure it's within the user's directory.
//     char file_path[512];
//     snprintf(file_path, sizeof(file_path), "user_files/%s/%s", username, file_name);
    
//     // Simple security check: prevent directory traversal.
//     if (strstr(file_path, "..")) {
//         return 0;
//     }

//     if (remove(file_path) == 0) {
//         printf("Debug: Successfully deleted file: %s\n", file_path);
//         return 1;
//     } else {
//         perror("Error deleting file");
//         return 0;
//     }
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = "";
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 // Increased buffer size to prevent truncation warning
//                 char item_html[2048]; 
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user_files/%s/%s\">%s</a>"
//                          "<form action=\"/delete_file\" method=\"post\" style=\"display:inline; margin-left: 10px;\">"
//                          "<input type=\"hidden\" name=\"filename\" value=\"%s\">"
//                          "<button type=\"submit\" style=\"background-color:#d9534f; color:white; border:none; padding:5px 10px; border-radius:4px; cursor:pointer;\">Delete</button>"
//                          "</form>"
//                          "</li>",
//                          username, dir->d_name, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     // Using a more robust method to generate the full HTML body
//     const char *dashboard_template_part1 =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "label, input { margin-bottom: 10px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         ".logout-form { text-align: right; margin-bottom: 20px; }"
//         ".logout-button { background-color: #d9534f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<div class=\"logout-form\">"
//         "<form action=\"/logout\" method=\"post\">"
//         "<button type=\"submit\" class=\"logout-button\">Logout</button>"
//         "</form>"
//         "</div>"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here. To update a file, simply upload a new one with the same name.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<label for=\"fileToUpload\">Choose a file to upload:</label>"
//         "<input type=\"file\" name=\"file_to_upload\" id=\"fileToUpload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
    
//     // Determine the required buffer size for the entire body
//     size_t body_len = snprintf(NULL, 0, dashboard_template_part1, username, file_list_html) + 1;
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     // Write the full HTML to the new buffer
//     snprintf(body, body_len, dashboard_template_part1, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }


// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     // 2. Find the start and end of the file data.
//     // The file data starts after the second \r\n\r\n
//     const char* part_headers_end = strstr(request, "\r\n\r\n");
//     if (!part_headers_end) return 0;
//     const char* data_start = part_headers_end + 4;

//     // Find the closing boundary line, which includes the trailing hyphens.
//     char closing_boundary_line[256];
//     snprintf(closing_boundary_line, sizeof(closing_boundary_line), "\r\n--%s--", boundary);

//     const char *file_end_boundary = strstr(data_start, closing_boundary_line);
//     if (!file_end_boundary) return 0;

//     // Calculate the length of the file content
//     size_t data_len = file_end_boundary - data_start;
    
//     // 3. Find the filename in the headers
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     char file_name[256];
//     size_t file_name_len = file_name_end - file_content_start;
    
//     // Check for an empty filename. This prevents the "Is a directory" error.
//     if (file_name_len == 0) {
//         return 0; // Return failure to prevent writing to a directory
//     }
    
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512];
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     // Only write if there is content to write
//     if (data_len > 0) {
//         fwrite(data_start, 1, data_len, fp);
//     }
    
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Handles the file deletion logic for a POST request.
//  */
// int http_handle_delete_file(const char* request, const char* username) {
//     // Parse the POST request body for the filename.
//     const char* body_start = strstr(request, "\r\n\r\n");
//     if (!body_start) return 0;
//     body_start += 4;
    
//     char file_name_urlencoded[256];
//     if (sscanf(body_start, "filename=%s", file_name_urlencoded) != 1) {
//         return 0; // Could not parse filename from body
//     }
    
//     char file_name[256];
//     urldecode(file_name, file_name_urlencoded);
    
//     // Construct the path and ensure it's within the user's directory.
//     char file_path[512];
//     snprintf(file_path, sizeof(file_path), "user_files/%s/%s", username, file_name);
    
//     // Simple security check: prevent directory traversal.
//     if (strstr(file_path, "..")) {
//         return 0;
//     }

//     if (remove(file_path) == 0) {
//         printf("Debug: Successfully deleted file: %s\n", file_path);
//         return 1;
//     } else {
//         perror("Error deleting file");
//         return 0;
//     }
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = "";
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 // Increased buffer size to prevent truncation warning
//                 char item_html[2048]; 
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user_files/%s/%s\">%s</a>"
//                          "<form action=\"/delete_file\" method=\"post\" style=\"display:inline; margin-left: 10px;\">"
//                          "<input type=\"hidden\" name=\"filename\" value=\"%s\">"
//                          "<button type=\"submit\" style=\"background-color:#d9534f; color:white; border:none; padding:5px 10px; border-radius:4px; cursor:pointer;\">Delete</button>"
//                          "</form>"
//                          "</li>",
//                          username, dir->d_name, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         ".logout-form { text-align: right; margin-bottom: 20px; }"
//         ".logout-button { background-color: #d9534f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<div class=\"logout-form\">"
//         "<form action=\"/logout\" method=\"post\">"
//         "<button type=\"submit\" class=\"logout-button\">Logout</button>"
//         "</form>"
//         "</div>"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here. To update a file, simply upload a new one with the same name.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }


// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     // 2. Find the start and end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline

//     // Find the closing boundary line, which includes the trailing hyphens.
//     char closing_boundary_line[256];
//     snprintf(closing_boundary_line, sizeof(closing_boundary_line), "\r\n--%s--", boundary);

//     const char *file_end_boundary = strstr(data_start, closing_boundary_line);
//     if (!file_end_boundary) return 0;

//     // Calculate the length of the file content
//     size_t data_len = file_end_boundary - data_start;
    
//     // 3. Find the filename in the headers
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     char file_name[256];
//     size_t file_name_len = file_name_end - file_content_start;
    
//     // Check for an empty filename. This is the root cause of the previous error.
//     if (file_name_len == 0) {
//         return 0; // Return failure to prevent writing to a directory
//     }
    
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512];
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     // Only write if there is content to write
//     if (data_len > 0) {
//         fwrite(data_start, 1, data_len, fp);
//     }
    
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = "";
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024];
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user_files/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         ".logout-form { text-align: right; margin-bottom: 20px; }"
//         ".logout-button { background-color: #d9534f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<div class=\"logout-form\">"
//         "<form action=\"/logout\" method=\"post\">"
//         "<button type=\"submit\" class=\"logout-button\">Logout</button>"
//         "</form>"
//         "</div>"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }



// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[256]; // Increased size
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512]; // Increased size
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = ""; // Increased size for file list
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024]; // Increased size
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user_files/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         ".logout-form { text-align: right; margin-bottom: 20px; }"
//         ".logout-button { background-color: #d9534f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<div class=\"logout-form\">"
//         "<form action=\"/logout\" method=\"post\">"
//         "<button type=\"submit\" class=\"logout-button\">Logout</button>"
//         "</form>"
//         "</div>"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     printf("Debug: Sending redirect with cookie. Location: %s, Cookie: session_id=%s\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[256]; // Increased size
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512]; // Increased size
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = ""; // Increased size for file list
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024]; // Increased size
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }




// #include <ctype.h> // For isxdigit()

// #define MAX_HTTP_HEADER_LEN 8192

// // Global buffer for error messages.
// extern char error_msg[256];

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[256]; // Increased size
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512]; // Increased size
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = ""; // Increased size for file list
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024]; // Increased size
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }

// #include "http_handler.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/stat.h>
// #include <dirent.h>
// #include <errno.h>
// #include <ctype.h> // For isxdigit()

// #define MAX_HTTP_HEADER_LEN 8192

// // Global buffer for error messages.
// extern char error_msg[256];

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[256]; // Increased size
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512]; // Increased size
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = ""; // Increased size for file list
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024]; // Increased size
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }
// #include "http_handler.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/stat.h>
// #include <dirent.h>
// #include <errno.h>
// #include <ctype.h> // For isxdigit()

// #define MAX_HTTP_HEADER_LEN 8192

// // Global buffer for error messages.
// extern char error_msg[256];

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[256]; // Increased size
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[512]; // Increased size
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[8192] = ""; // Increased size for file list
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[1024]; // Increased size
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }


// // http_handler.c
// // Implements HTTP parsing, response sending, and file handling.

// #include "http_handler.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <errno.h>  
// #include<ctype.h>
// #include <unistd.h>
// #include <sys/stat.h>
// #include <dirent.h>

// #define MAX_HTTP_HEADER_LEN 8192

// // Global buffer for error messages.
// extern char error_msg[256];

// /**
//  * Dynamically resizes a buffer.
//  */
// static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
//     char* new_buffer = realloc(buffer, new_size);
//     if (!new_buffer) {
//         free(buffer);
//         snprintf(error_msg, sizeof(error_msg), "Failed to reallocate buffer.");
//         return NULL;
//     }
//     return new_buffer;
// }

// /**
//  * Reads the full HTTP request from a socket.
//  */
// char* read_full_request(int c) {
//     char* request = NULL;
//     size_t buffer_size = 1024;
//     size_t total_received = 0;
    
//     request = malloc(buffer_size);
//     if (!request) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for request.");
//         return NULL;
//     }
    
//     int bytes_received;
//     while ((bytes_received = recv(c, request + total_received, buffer_size - total_received - 1, 0)) > 0) {
//         total_received += bytes_received;
//         request[total_received] = '\0';
        
//         // Check for end of headers
//         if (strstr(request, "\r\n\r\n")) {
//             // Found the end of headers, now check for body
//             char* content_length_str = strstr(request, "Content-Length:");
//             if (content_length_str) {
//                 int content_length = atoi(content_length_str + strlen("Content-Length:"));
//                 size_t headers_length = strstr(request, "\r\n\r\n") - request + 4;
//                 if (total_received >= headers_length + content_length) {
//                     break;
//                 }
//             } else {
//                 break; // No content-length, so headers are the end
//             }
//         }
        
//         // Resize buffer if needed
//         if (total_received >= buffer_size - 1) {
//             buffer_size *= 2;
//             request = reallocate_buffer(request, total_received, buffer_size);
//             if (!request) return NULL;
//         }
//     }
    
//     if (bytes_received < 0) {
//         free(request);
//         snprintf(error_msg, sizeof(error_msg), "Recv() error: %s", strerror(errno));
//         return NULL;
//     }
    
//     return request;
// }

// /**
//  * Parses a simple HTTP request and returns a httpreq struct.
//  */
// httpreq *parse_http(const char *request) {
//     if (!request) return NULL;
    
//     httpreq *req = malloc(sizeof(httpreq));
//     if (!req) {
//         snprintf(error_msg, sizeof(error_msg), "Failed to allocate memory for httpreq.");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));
    
//     char *request_copy = strdup(request);
//     if (!request_copy) {
//         free(req);
//         snprintf(error_msg, sizeof(error_msg), "Failed to duplicate request string.");
//         return NULL;
//     }
    
//     char *line = strtok(request_copy, "\r\n");
//     if (line) {
//         sscanf(line, "%s %s %*s", req->method, req->url);
//     }
    
//     free(request_copy);
//     return req;
// }

// /**
//  * Sends an HTTP response.
//  */
// void http_send_response(int c, int status_code, const char *content_type, const char *body, size_t body_len) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 %d %s\r\n"
//              "Content-Type: %s\r\n"
//              "Content-Length: %zu\r\n"
//              "Connection: close\r\n"
//              "\r\n",
//              status_code, get_status_message(status_code), content_type, body_len);
//     send(c, header, strlen(header), 0);
//     send(c, body, body_len, 0);
// }

// /**
//  * Sends an HTTP redirect response.
//  */
// void http_send_redirect(int c, const char *url) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Connection: close\r\n"
//              "\r\n", url);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Sends an HTTP redirect response with a session cookie.
//  */
// void http_send_redirect_with_cookie(int c, const char *url, const char *session_id) {
//     char header[MAX_HTTP_HEADER_LEN];
//     snprintf(header, sizeof(header),
//              "HTTP/1.1 302 Found\r\n"
//              "Location: %s\r\n"
//              "Set-Cookie: session_id=%s; HttpOnly; Path=/\r\n"
//              "Connection: close\r\n"
//              "\r\n", url, session_id);
//     send(c, header, strlen(header), 0);
// }

// /**
//  * Gets the status message for a given status code.
//  */
// const char* get_status_message(int status_code) {
//     switch (status_code) {
//         case 200: return "OK";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 500: return "Internal Server Error";
//         default: return "Unknown";
//     }
// }

// /**
//  * Gets the Content-Type for a given filename.
//  */
// const char* get_content_type(const char* filename) {
//     const char* ext = strrchr(filename, '.');
//     if (!ext) return "application/octet-stream";
//     if (strcmp(ext, ".html") == 0) return "text/html";
//     if (strcmp(ext, ".css") == 0) return "text/css";
//     if (strcmp(ext, ".js") == 0) return "application/javascript";
//     if (strcmp(ext, ".png") == 0) return "image/png";
//     if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
//     if (strcmp(ext, ".gif") == 0) return "image/gif";
//     return "application/octet-stream";
// }

// /**
//  * URL-decodes a string in-place.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if ((*src == '%') &&
//             ((a = src[1]) && (b = src[2])) &&
//             (isxdigit(a) && isxdigit(b))) {
//             if (a >= 'a')
//                 a -= 'a' - 'A';
//             if (a >= 'A')
//                 a -= 'A' - 10;
//             else
//                 a -= '0';
//             if (b >= 'a')
//                 b -= 'a' - 'A';
//             if (b >= 'A')
//                 b -= 'A' - 10;
//             else
//                 b -= '0';
//             *dst++ = 16 * a + b;
//             src += 3;
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Reads a file into a File struct.
//  */
// File* fileread(const char* filename) {
//     struct stat st;
//     if (stat(filename, &st) == -1) {
//         return NULL;
//     }
    
//     FILE* fp = fopen(filename, "rb");
//     if (!fp) {
//         return NULL;
//     }
    
//     File* f = malloc(sizeof(File));
//     if (!f) {
//         fclose(fp);
//         return NULL;
//     }
    
//     f->size = st.st_size;
//     f->fc = malloc(f->size);
//     if (!f->fc) {
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     f->filename = strdup(filename);
//     if (!f->filename) {
//         free(f->fc);
//         free(f);
//         fclose(fp);
//         return NULL;
//     }
    
//     size_t read_size = fread(f->fc, 1, f->size, fp);
//     fclose(fp);
//     if (read_size != f->size) {
//         free(f->fc);
//         free(f->filename);
//         free(f);
//         return NULL;
//     }
    
//     return f;
// }

// /**
//  * Handles the file upload logic for a multipart/form-data request.
//  */
// int http_handle_upload(const char *request, const char* username) {
//     // 1. Find the multipart boundary
//     const char *boundary_start = strstr(request, "boundary=");
//     if (!boundary_start) return 0;
//     boundary_start += strlen("boundary=");
//     char boundary[128];
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end) return 0;
//     size_t boundary_len = boundary_end - boundary_start;
//     strncpy(boundary, boundary_start, boundary_len);
//     boundary[boundary_len] = '\0';
    
//     char boundary_line[256];
//     snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    
//     // 2. Find the start of the file data
//     const char *file_content_start = strstr(request, "filename=\"");
//     if (!file_content_start) return 0;
//     file_content_start += strlen("filename=\"");
//     char *file_name_end = strchr(file_content_start, '\"');
//     if (!file_name_end) return 0;
    
//     // Extract filename
//     char file_name[128];
//     size_t file_name_len = file_name_end - file_content_start;
//     strncpy(file_name, file_content_start, file_name_len);
//     file_name[file_name_len] = '\0';
    
//     // 3. Find the end of the file data
//     const char *data_start = strstr(request, "\r\n\r\n");
//     if (!data_start) return 0;
//     data_start += 4; // Move past the double newline
    
//     const char *file_end_boundary = strstr(data_start, boundary_line);
//     if (!file_end_boundary) return 0;
    
//     // 4. Create the user's directory if it doesn't exist
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     if (mkdir(user_dir, 0777) != 0 && errno != EEXIST) {
//         perror("Error creating user directory");
//         return 0;
//     }
    
//     // 5. Write the file content to disk
//     char file_path[256];
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, file_name);
    
//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }
    
//     size_t data_len = file_end_boundary - data_start - 2; // -2 for trailing \r\n
//     fwrite(data_start, 1, data_len, fp);
//     fclose(fp);
    
//     return 1;
// }

// /**
//  * Generates and sends the dashboard HTML page with the user's files.
//  */
// void http_send_dashboard(int c, const char* username) {
//     char user_dir[256];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
    
//     char file_list_html[4096] = ""; // Buffer for the list of files.
//     DIR *d = opendir(user_dir);
//     if (d) {
//         struct dirent *dir;
//         while ((dir = readdir(d)) != NULL) {
//             if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
//                 char item_html[512];
//                 // Using a dynamic buffer for safety.
//                 snprintf(item_html, sizeof(item_html),
//                          "<li><a href=\"/user/%s/%s\">%s</a></li>",
//                          username, dir->d_name, dir->d_name);
//                 strcat(file_list_html, item_html);
//             }
//         }
//         closedir(d);
//     } else {
//         // If the directory doesn't exist, show an empty list and a message.
//         strcat(file_list_html, "<li>No files uploaded yet.</li>");
//     }
    
//     char *dashboard_template =
//         "<!DOCTYPE html>"
//         "<html><head><title>Dashboard</title><style>"
//         "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f4; }"
//         ".container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
//         "h1 { color: #333; }"
//         "form { margin-top: 20px; }"
//         "input[type='file'] { border: 1px solid #ccc; padding: 10px; border-radius: 4px; }"
//         "input[type='submit'] { background-color: #5cb85c; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
//         "ul { list-style-type: none; padding: 0; }"
//         "li { background: #eee; margin-top: 5px; padding: 10px; border-radius: 4px; }"
//         "</style></head><body>"
//         "<div class=\"container\">"
//         "<h1>Welcome, %s!</h1>"
//         "<p>This is your personal dashboard. You can upload and view your files here.</p>"
//         "<h2>Upload a File</h2>"
//         "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">"
//         "<input type=\"file\" name=\"file_to_upload\">"
//         "<input type=\"submit\" value=\"Upload\">"
//         "</form>"
//         "<h2>Your Files</h2>"
//         "<ul>%s</ul>"
//         "</div></body></html>";
        
//     size_t body_len = strlen(dashboard_template) + strlen(username) + strlen(file_list_html) - 4; // -4 for the %s placeholders
//     char *body = malloc(body_len);
//     if (!body) {
//         http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//         return;
//     }
    
//     snprintf(body, body_len, dashboard_template, username, file_list_html);
//     http_send_response(c, 200, "text/html", body, strlen(body));
//     free(body);
// }




// #include "http_handler.h"
// #include <stdlib.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <string.h>
// #include <errno.h>
// #include <sys/socket.h>
// #include <dirent.h> // For directory operations
// #include <sys/stat.h> // For mkdir()

// #define MAX_REQUEST_SIZE 4096

// // Global error message buffer.
// extern char error_msg[256];

// /**
//  * Helper function to decode URL-encoded characters.
//  * @param dst The destination buffer for the decoded string.
//  * @param src The source URL-encoded string.
//  */
// void urldecode(char *dst, const char *src) {
//     char a, b;
//     while (*src) {
//         if (*src == '%') {
//             if (sscanf(src + 1, "%2hhx", &a) == 1) {
//                 *dst++ = a;
//                 src += 3;
//             } else {
//                 *dst++ = *src++;
//             }
//         } else if (*src == '+') {
//             *dst++ = ' ';
//             src++;
//         } else {
//             *dst++ = *src++;
//         }
//     }
//     *dst = '\0';
// }

// /**
//  * Parses the HTTP request header to find the method and URL.
//  * @param str The full request string.
//  * @return A new httpreq struct, or NULL on error.
//  */
// httpreq *parse_http(char *str) {
//     httpreq *req;
//     char *p = str;
    
//     req = malloc(sizeof(httpreq));
//     if (req == NULL) {
//         snprintf(error_msg, sizeof(error_msg), "parse_http() error: memory allocation failed");
//         return NULL;
//     }
//     memset(req, 0, sizeof(httpreq));

//     char *method_end = strchr(p, ' ');
//     if (!method_end) {
//         free(req);
//         return NULL;
//     }
//     int method_len = method_end - p;
//     strncpy(req->method, p, method_len);
//     req->method[method_len] = '\0';

//     p = method_end + 1;
//     char *url_end = strchr(p, ' ');
//     if (!url_end) {
//         free(req);
//         return NULL;
//     }
//     int url_len = url_end - p;
//     strncpy(req->url, p, url_len);
//     req->url[url_len] = '\0';

//     return req;
// }


// /**
//  * Sends an HTTP redirect response.
//  * @param c The client socket file descriptor.
//  * @param url The URL to redirect to.
//  */
// void http_send_redirect(int c, const char *url){
// char header_buf[1024] ;
// int n;
// snprintf(header_buf, sizeof(header_buf)-1,
// "HTTP/1.0 302 Found \r\n"
// "Location: %s \r\n"
// "Content-Length: 0 \r\n"
// "\r\n"
// ,url
// );
// n= strlen(header_buf);
// write(c,header_buf,n);
// }


// /**
//  * Reads the entire HTTP request from the client socket.
//  * @param c The client socket file descriptor.
//  * @return A dynamically allocated string with the full request, or NULL on error.
//  */
// char *read_full_request(int c) {
//     char *request = NULL;
//     size_t total_size = 0;
//     ssize_t bytes_read;
//     char buffer[4096];
//     char *header_end;

//     while (1) {
//         bytes_read = recv(c, buffer, sizeof(buffer), 0);
//         if (bytes_read <= 0) {
//             if (request) free(request);
//             return NULL;
//         }

//         char *temp_request = realloc(request, total_size + bytes_read + 1);
//         if (!temp_request) {
//             perror("realloc() failed");
//             if (request) free(request);
//             return NULL;
//         }
//         request = temp_request;
//         memcpy(request + total_size, buffer, bytes_read);
//         total_size += bytes_read;
//         request[total_size] = '\0';

//         header_end = strstr(request, "\r\n\r\n");
//         if (header_end != NULL) {
//             break;
//         }

//         if (total_size >= MAX_REQUEST_SIZE) {
//             fprintf(stderr, "Request size exceeds limit.\n");
//             if (request) free(request);
//             return NULL;
//         }
//     }
    
//     char *cl_header = strstr(request, "Content-Length: ");
//     if (cl_header) {
//         int content_length = atoi(cl_header + strlen("Content-Length: "));
//         size_t body_start_offset = (header_end - request) + 4;
//         size_t current_body_size = total_size - body_start_offset;
//         int remaining_bytes = content_length - current_body_size;
//         if (remaining_bytes > 0) {
//             char *temp_request = realloc(request, total_size + remaining_bytes + 1);
//             if (!temp_request) {
//                 perror("realloc() failed for body");
//                 free(request);
//                 return NULL;
//             }
//             request = temp_request;
//             bytes_read = recv(c, request + total_size, remaining_bytes, 0);
//             if (bytes_read > 0) {
//                 total_size += bytes_read;
//                 request[total_size] = '\0';
//             }
//         }
//     }
//     return request;
// }

// /**
//  * Sends the HTTP status line, headers, and data to the client.
//  * @param c The client socket file descriptor.
//  * @param code The HTTP status code.
//  * @param contentType The Content-Type header value.
//  * @param data The response body.
//  * @param data_length The size of the response body in bytes.
//  */
// void http_send_response(int c, int code, const char *contentType, const char *data, int data_length) {
//     char header_buf[1024];
//     int n;
//     snprintf(header_buf, sizeof(header_buf) - 1,
//         "HTTP/1.0 %d OK\r\n"
//         "Server: httpd.c\r\n"
//         "Content-Type: %s\r\n"
//         "Content-Length: %d\r\n"
//         "\r\n",
//         code, contentType, data_length
//     );
//     n = strlen(header_buf);
//     write(c, header_buf, n);
//     write(c, data, data_length); 
// }

// /**
//  * Dynamically generates and sends the dashboard page with the user's name and file list.
//  * @param c The client socket file descriptor.
//  * @param username The name of the logged-in user.
//  */
// void http_send_dashboard(int c, const char *username) {
//     const char *dashboard_html = 
// "<!DOCTYPE html>\n"
// "<html lang=\"en\">\n"
// "<head>\n"
// "    <meta charset=\"UTF-8\">\n"
// "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
// "    <title>Dashboard</title>\n"
// "    <style>\n"
// "        body {\n"
// "            font-family: sans-serif;\n"
// "            background-color: #f4f4f4;\n"
// "            display: flex;\n"
// "            justify-content: center;\n"
// "            align-items: center;\n"
// "            height: 100vh;\n"
// "            margin: 0;\n"
// "            color: #333;\n"
// "        }\n"
// "        .dashboard-container {\n"
// "            background-color: #fff;\n"
// "            padding: 2rem;\n"
// "            border-radius: 8px;\n"
// "            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);\n"
// "            max-width: 800px;\n"
// "            width: 100vw;\n"
// "            display: flex;\n"
// "            flex-direction: column;\n"
// "            gap: 1.5rem;\n"
// "        }\n"
// "        .header {\n"
// "            display: flex;\n"
// "            justify-content: space-between;\n"
// "            align-items: center;\n"
// "            border-bottom: 1px solid #eee;\n"
// "            padding-bottom: 1rem;\n"
// "        }\n"
// "        h1 {\n"
// "            color: #2c3e50;\n"
// "            font-size: 1.8rem;\n"
// "            margin: 0;\n"
// "        }\n"
// "        .logout-button {\n"
// "            text-decoration: none;\n"
// "            background-color: #e74c3c;\n"
// "            color: white;\n"
// "            padding: 0.5rem 1rem;\n"
// "            border-radius: 4px;\n"
// "            font-weight: bold;\n"
// "            transition: background-color 0.3s ease;\n"
// "        }\n"
// "        .logout-button:hover {\n"
// "            background-color: #c0392b;\n"
// "        }\n"
// "        .upload-form-section {\n"
// "            padding: 1rem 0;\n"
// "            border-bottom: 1px solid #eee;\n"
// "        }\n"
// "        .upload-form-section h2 {\n"
// "            font-size: 1.2rem;\n"
// "            margin-top: 0;\n"
// "        }\n"
// "        .file-upload-form {\n"
// "            display: flex;\n"
// "            gap: 0.5rem;\n"
// "            align-items: center;\n"
// "        }\n"
// "        .file-upload-form input[type=\"file\"] {\n"
// "            flex-grow: 1;\n"
// "        }\n"
// "        .file-upload-form button {\n"
// "            background-color: #3498db;\n"
// "            color: white;\n"
// "            padding: 0.5rem 1rem;\n"
// "            border: none;\n"
// "            border-radius: 4px;\n"
// "            cursor: pointer;\n"
// "            font-weight: bold;\n"
// "            transition: background-color 0.3s ease;\n"
// "        }\n"
// "        .file-upload-form button:hover {\n"
// "            background-color: #2980b9;\n"
// "        }\n"
// "        .file-list-section {\n"
// "            padding: 1rem 0;\n"
// "        }\n"
// "        .file-list-section h2 {\n"
// "            font-size: 1.2rem;\n"
// "            margin-top: 0;\n"
// "        }\n"
// "        .file-list {\n"
// "            list-style: none;\n"
// "            padding: 0;\n"
// "            margin: 0;\n"
// "        }\n"
// "        .file-item {\n"
// "            background-color: #ecf0f1;\n"
// "            padding: 0.75rem;\n"
// "            border-radius: 4px;\n"
// "            margin-bottom: 0.5rem;\n"
// "            display: flex;\n"
// "            justify-content: space-between;\n"
// "            align-items: center;\n"
// "        }\n"
// "        .file-item a {\n"
// "            color: #2980b9;\n"
// "            text-decoration: none;\n"
// "            font-weight: bold;\n"
// "        }\n"
// "    </style>\n"
// "</head>\n"
// "<body>\n"
// "\n"
// "<div class=\"dashboard-container\">\n"
// "    <div class=\"header\">\n"
// "        <h1>Welcome, %s!</h1>\n"
// "        <a href=\"/login.html\" class=\"logout-button\">Logout</a>\n"
// "    </div>\n"
// "\n"
// "    <div class=\"upload-form-section\">\n"
// "        <h2>Upload a new file</h2>\n"
// "        <form action=\"/upload\" method=\"POST\" class=\"file-upload-form\" enctype=\"multipart/form-data\">\n"
// "            <input type=\"file\" name=\"uploaded_file\" required>\n"
// "            <button type=\"submit\">Upload</button>\n"
// "        </form>\n"
// "    </div>\n"
// "\n"
// "    <div class=\"file-list-section\">\n"
// "        <h2>Your Hosted Files</h2>\n"
// "        <ul class=\"file-list\">\n"
// "            %s\n"
// "        </ul>\n"
// "    </div>\n"
// "</div>\n"
// "\n"
// "</body>\n"
// "</html>";

//     File *f;
//     char user_dir[128];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);

//     // Build the dynamic file list
//     char *file_list_html = malloc(1); // Start with a small buffer
//     size_t file_list_len = 0;
//     file_list_html[0] = '\0';
    
//     DIR *d;
//     struct dirent *dir;
//     d = opendir(user_dir);
//     if (d) {
//         while ((dir = readdir(d)) != NULL) {
//             if (dir->d_type == DT_REG) { // Check if it's a regular file
//                 // Use snprintf to get the required size first
//                 size_t li_item_len = snprintf(NULL, 0,
//                     "<li class=\"file-item\"><span>%s</span><a href=\"/user/%s/%s\" target=\"_blank\">View</a></li>\n",
//                     dir->d_name, username, dir->d_name);
                
//                 // Reallocate the main buffer to fit the new item
//                 char *temp_realloc = realloc(file_list_html, file_list_len + li_item_len + 1);
//                 if (temp_realloc == NULL) {
//                     // Handle allocation failure
//                     free(file_list_html);
//                     closedir(d);
//                     http_send_response(c, 500, "text/plain", "Internal Server Error", 21);
//                     return;
//                 }
//                 file_list_html = temp_realloc;

//                 // Write the item to the new buffer
//                 snprintf(file_list_html + file_list_len, li_item_len + 1,
//                     "<li class=\"file-item\"><span>%s</span><a href=\"/user/%s/%s\" target=\"_blank\">View</a></li>\n",
//                     dir->d_name, username, dir->d_name);
                
//                 file_list_len += li_item_len;
//             }
//         }
//         closedir(d);
//     } else {
//         // Directory doesn't exist, this is fine on first login/upload
//     }

//     // Build the final response with the username and file list
//     size_t response_len = snprintf(NULL, 0, dashboard_html, username, file_list_html);
//     char *response_buffer = malloc(response_len + 1);
//     snprintf(response_buffer, response_len + 1, dashboard_html, username, file_list_html);
    
//     http_send_response(c, 200, "text/html", response_buffer, response_len);

//     free(response_buffer);
//     free(file_list_html);
// }

// /**
//  * Handles multipart file upload requests.
//  * @param request_data The full HTTP request body.
//  * @param username The name of the user uploading the file.
//  * @return 1 on success, 0 on failure.
//  */
// int http_handle_upload(const char *request_data, const char *username) {
//     char boundary[256];
//     char *ct_header_line = strstr(request_data, "Content-Type: multipart/form-data; boundary=");
//     if (!ct_header_line) {
//         return 0;
//     }
    
//     char *boundary_start = ct_header_line + strlen("Content-Type: multipart/form-data; boundary=");
//     char *boundary_end = strchr(boundary_start, '\r');
//     if (!boundary_end || (boundary_end - boundary_start) >= sizeof(boundary)) {
//         return 0;
//     }
//     strncpy(boundary, boundary_start, boundary_end - boundary_start);
//     boundary[boundary_end - boundary_start] = '\0';
    
//     char boundary_line_start[512]; // Increased size
//     snprintf(boundary_line_start, sizeof(boundary_line_start), "\r\n--%s", boundary);
//     char boundary_line_end[512]; // Increased size
//     snprintf(boundary_line_end, sizeof(boundary_line_end), "\r\n--%s--", boundary);

//     char *body_start = strstr(request_data, "\r\n\r\n");
//     if (!body_start) {
//         return 0;
//     }
//     body_start += 4;
    
//     char *file_header_end = strstr(body_start, "\r\n\r\n");
//     if (!file_header_end) {
//         return 0;
//     }
    
//     char *filename_start = strstr(body_start, "filename=\"");
//     if (!filename_start) {
//         return 0;
//     }
//     filename_start += strlen("filename=\"");
    
//     char *filename_end = strchr(filename_start, '\"');
//     char filename[128];
//     // char filename[200] = filename_end - filename_start; 
//     if (!filename_end || (filename_end - filename_start) >= sizeof(filename)) {
//         return 0;
//     }
   
//     strncpy(filename, filename_start, filename_end - filename_start);
//     filename[filename_end - filename_start] = '\0';

//     char *file_data_start = file_header_end + 4;
//     char *file_data_end = strstr(file_data_start, boundary_line_end);
//     if (!file_data_end) {
//         return 0;
//     }

//     size_t file_len = file_data_end - file_data_start;
    
//     char user_dir[128];
//     snprintf(user_dir, sizeof(user_dir), "user_files/%s", username);
//     mkdir(user_dir, 0777); // Create the user directory if it doesn't exist

//     char file_path[256];
//     snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, filename);

//     FILE *fp = fopen(file_path, "wb");
//     if (!fp) {
//         perror("Error opening file for writing");
//         return 0;
//     }

//     fwrite(file_data_start, 1, file_len, fp);
//     fclose(fp);

//     return 1;
// }



// /**
//  * Determines the Content-Type based on a file extension.
//  * @param path The file path.
//  * @return A string containing the correct MIME type.
//  */
// const char *get_content_type(const char *path) {
//     const char *extension = strrchr(path, '.');
//     if (extension == NULL) {
//         return "text/plain";
//     }
//     if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) return "text/html";
//     if (strcmp(extension, ".css") == 0) return "text/css";
//     if (strcmp(extension, ".js") == 0) return "application/javascript";
//     if (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0) return "image/jpeg";
//     if (strcmp(extension, ".png") == 0) return "image/png";
//     if (strcmp(extension, ".gif") == 0) return "image/gif";
//     if (strcmp(extension, ".mp4") == 0) return "video/mp4";
//     return "text/plain";
// }

// /**
//  * Reads the entire contents of a file into a dynamically allocated struct.
//  * @param filename The path to the file to read.
//  * @return A pointer to a new File struct, or NULL on error.
//  */
// File *fileread(char *filename) {
//     int n, fd;
//     File *f;
//     f = malloc(sizeof(File));
//     if (f == NULL) {
//         perror("malloc() error for File struct");
//         return NULL;
//     }
//     fd = open(filename, O_RDONLY);
//     if (fd < 0) {
//         perror("open() error");
//         free(f);
//         return NULL;
//     }
//     strncpy(f->filename, filename, sizeof(f->filename) - 1);
//     f->filename[sizeof(f->filename) - 1] = '\0';
//     f->fc = malloc(1);
//     f->size = 0;
//     char temp_buf[512];
//     while ((n = read(fd, temp_buf, sizeof(temp_buf))) > 0) {
//         void *realloc_ptr = realloc(f->fc, f->size + n);
//         if (realloc_ptr == NULL) {
//             perror("realloc() error");
//             close(fd);
//             free(f->fc);
//             free(f);
//             return NULL;
//         }
//         f->fc = realloc_ptr;
//         memcpy(f->fc + f->size, temp_buf, n);
//         f->size += n;
//     }
//     if (n < 0) {
//         perror("read() error");
//         close(fd);
//         free(f->fc);
//         free(f);
//         return NULL;
//     }
//     close(fd);
//     f->fc[f->size] = '\0';
//     return f;
// }



// // #include <stdio.h>      // Standard input/output library
// // #include <stdlib.h>     // Standard library for functions like exit()
// // #include <netinet/in.h> // Sockets library for internet addresses
// // #include <sys/types.h>  // System data types
// // #include <fcntl.h>      // File control options
// // #include <sys/socket.h> // Core sockets library
// // #include <unistd.h>     // POSIX API, includes fork()
// // #include <string.h>     // String manipulation functions
// // #include <arpa/inet.h>  // For inet_addr
// // #include <errno.h>      // For error codes like EAGAIN
// // #include <time.h>       // For getting the current time
// // #include <sys/time.h>   // for timeval struct

// // #include "http_handler.h"

// // // Global error message buffer.
// // // Note: In a real-world, multi-threaded server, this would be unsafe.
// // // For a multi-process server using fork(), each process gets its own copy, so it's safe.
// // char error_msg[256];

// // // Helper function to decode URL-encoded characters.
// // // It converts characters like %20 to spaces.
// // void urldecode(char *dst, const char *src) {
// //     char a, b;
// //     while (*src) {
// //         if (*src == '%') {
// //             if (sscanf(src + 1, "%2hhx", &a) == 1) {
// //                 *dst++ = a;
// //                 src += 3;
// //             } else {
// //                 *dst++ = *src++;
// //             }
// //         } else if (*src == '+') {
// //             *dst++ = ' ';
// //             src++;
// //         } else {
// //             *dst++ = *src++;
// //         }
// //     }
// //     *dst = '\0';
// // }

// // /**
// //  * Parses the HTTP request header to find the method and URL.
// //  * @param str The full request string.
// //  * @return A new httpreq struct, or NULL on error.
// //  */
// // httpreq *parse_http(char *str) {
// //     httpreq *req;
// //     char *p = str;
    
// //     req = malloc(sizeof(httpreq));
// //     if (req == NULL) {
// //         snprintf(error_msg, sizeof(error_msg), "parse_http() error: memory allocation failed");
// //         return NULL;
// //     }
// //     memset(req, 0, sizeof(httpreq));

// //     // Read method (first word)
// //     char *method_end = strchr(p, ' ');
// //     if (!method_end) {
// //         free(req);
// //         return NULL;
// //     }
// //     int method_len = method_end - p;
// //     strncpy(req->method, p, method_len);
// //     req->method[method_len] = '\0';

// //     // Read URL (second word)
// //     p = method_end + 1;
// //     char *url_end = strchr(p, ' ');
// //     if (!url_end) {
// //         free(req);
// //         return NULL;
// //     }
// //     int url_len = url_end - p;
// //     strncpy(req->url, p, url_len);
// //     req->url[url_len] = '\0';

// //     return req;
// // }

// // /**
// //  * Reads the entire HTTP request from the client socket.
// //  * This is crucial for POST requests as it includes the body.
// //  * It reads until the connection closes or the buffer is full.
// //  * @param c The client socket file descriptor.
// //  * @return A dynamically allocated string with the full request, or NULL on error.
// //  */
// // // A more robust function to read the full HTTP request. It reads headers first,
// // // then uses Content-Length to read the exact body size.
// // char *read_full_request(int c) {
// //     char *request = NULL;
// //     size_t total_size = 0;
// //     ssize_t bytes_read;
// //     char buffer[4096];
// //     char *header_end;

// //     // Read the request until the header delimiter "\r\n\r\n" is found
// //     // or the request size limit is reached.
// //     while (1) {
// //         bytes_read = recv(c, buffer, sizeof(buffer), 0);
// //         if (bytes_read <= 0) {
// //             if (request) free(request);
// //             return NULL;
// //         }

// //         // Reallocate memory for the request buffer
// //         char *temp_request = realloc(request, total_size + bytes_read + 1);
// //         if (!temp_request) {
// //             perror("realloc() failed");
// //             if (request) free(request);
// //             return NULL;
// //         }
// //         request = temp_request;
// //         memcpy(request + total_size, buffer, bytes_read);
// //         total_size += bytes_read;
// //         request[total_size] = '\0';

// //         header_end = strstr(request, "\r\n\r\n");
// //         if (header_end != NULL) {
// //             // Found the end of headers, break the loop
// //             break;
// //         }

// //         // Check for request size limit
// //         if (total_size >= MAX_REQUEST_SIZE) {
// //             fprintf(stderr, "Request size exceeds limit.\n");
// //             if (request) free(request);
// //             return NULL;
// //         }
// //     }
    
// //     // Check if the request is a POST request and has a body
// //     char *cl_header = strstr(request, "Content-Length: ");
// //     if (cl_header) {
// //         int content_length = atoi(cl_header + strlen("Content-Length: "));
// //         // Calculate the size of the body data already read with the headers
// //         size_t body_start_offset = (header_end - request) + 4;
// //         size_t current_body_size = total_size - body_start_offset;
// //         int remaining_bytes = content_length - current_body_size;
        
// //         // Read the rest of the body if not all of it was received
// //         if (remaining_bytes > 0) {
// //             char *temp_request = realloc(request, total_size + remaining_bytes + 1);
// //             if (!temp_request) {
// //                 perror("realloc() failed for body");
// //                 free(request);
// //                 return NULL;
// //             }
// //             request = temp_request;
// //             bytes_read = recv(c, request + total_size, remaining_bytes, 0);
// //             if (bytes_read > 0) {
// //                 total_size += bytes_read;
// //                 request[total_size] = '\0';
// //             }
// //         }
// //     }

// //     return request;
// // }
// // /**
// //  * Sends the HTTP status line, headers, and data to the client.
// //  * @param c The client socket file descriptor.
// //  * @param code The HTTP status code.
// //  * @param contentType The Content-Type header value.
// //  * @param data The response body.
// //  * @param data_length The size of the response body in bytes.
// //  */
// // void http_send_response(int c, int code, const char *contentType, const char *data, int data_length) {
// //     char header_buf[1024];
// //     int n;

// //     snprintf(header_buf, sizeof(header_buf) - 1,
// //         "HTTP/1.0 %d OK\r\n"
// //         "Server: httpd.c\r\n"
// //         "Content-Type: %s\r\n"
// //         "Content-Length: %d\r\n"
// //         "\r\n", // The crucial blank line
// //         code, contentType, data_length
// //     );

// //     n = strlen(header_buf);
// //     write(c, header_buf, n);
// //     write(c, data, data_length); 
// // }

// // /**
// //  * Determines the Content-Type based on a file extension.
// //  * @param path The file path.
// //  * @return A string containing the correct MIME type.
// //  */
// // const char *get_content_type(const char *path) {
// //     const char *extension = strrchr(path, '.');
// //     if (extension == NULL) {
// //         return "text/plain";
// //     }

// //     if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) return "text/html";
// //     if (strcmp(extension, ".css") == 0) return "text/css";
// //     if (strcmp(extension, ".js") == 0) return "application/javascript";
// //     if (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0) return "image/jpeg";
// //     if (strcmp(extension, ".png") == 0) return "image/png";
// //     if (strcmp(extension, ".gif") == 0) return "image/gif";
// //     if (strcmp(extension, ".mp4") == 0) return "video/mp4";
    
// //     return "text/plain";
// // }