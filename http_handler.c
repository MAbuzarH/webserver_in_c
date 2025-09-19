// // http_handler.c
// // Implements HTTP request parsing and response sending logic.

#include "http_handler.h"
#include "thread_safe.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>

#define BUFFER_SIZE 8192
#define MAX_USERNAME_LENGTH 63


// URL encode function
void urlencode(char *dest, const char *src) {
    const char *hex = "0123456789ABCDEF";
    size_t i = 0, j = 0;
    
    while (src[i] && j < 255) {
        if (isalnum(src[i]) || src[i] == '-' || src[i] == '_' || src[i] == '.' || src[i] == '~') {
            dest[j++] = src[i];
        } else if (src[i] == ' ') {
            dest[j++] = '+';
        } else {
            dest[j++] = '%';
            dest[j++] = hex[(src[i] >> 4) & 0xF];
            dest[j++] = hex[src[i] & 0xF];
        }
        i++;
    }
    dest[j] = '\0';
}


// Helper function to find a header in the request
const char *find_header(const char *request, const char *header_name) {
    char search_str[256];
    snprintf(search_str, sizeof(search_str), "%s", header_name);
    
    const char *header_start = strstr(request, search_str);
    if (!header_start) {
        return NULL;
    }
    
    header_start += strlen(search_str);
    
    // Skip any whitespace or colons
    while (*header_start == ' ' || *header_start == ':') {
        header_start++;
    }
    
    // Find the end of the header value
    const char *header_end = strstr(header_start, "\r\n");
    if (!header_end) {
        return NULL;
    }
    
    // Copy the header value (temporary solution)
    static char header_value[256];
    size_t len = header_end - header_start;
    if (len >= sizeof(header_value)) {
        len = sizeof(header_value) - 1;
    }
    
    strncpy(header_value, header_start, len);
    header_value[len] = '\0';
    
    return header_value;
}



static char* reallocate_buffer(char* buffer, size_t old_size, size_t new_size) {
    char* new_buffer = realloc(buffer, new_size);
    if (!new_buffer) {
        free(buffer);
        perror("Failed to reallocate buffer.");
        return NULL;
    }
    return new_buffer;
}
/**
 * @brief Reads the entire HTTP request from a socket.
 * @param client_socket The client socket file descriptor.
 * @return A dynamically allocated string containing the full request, or NULL on error.
 */
char* read_full_request(int c, size_t *request_size) {
    char* request = NULL;
    size_t buffer_size = 1024;
    size_t total_received = 0;
    
    request = malloc(buffer_size);
    if (!request) {
        perror("Failed to allocate memory for request.");
        return NULL;
    }
    
    int bytes_received;
    int headers_complete = 0;
    size_t headers_length = 0;
    size_t expected_total = 0;
    
    while ((bytes_received = recv(c, request + total_received, 
                                 buffer_size - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
        
        if (!headers_complete) {
            // Null terminate for string operations (only for header parsing)
            request[total_received] = '\0';
            
            // Check for end of headers
            char *header_end = strstr(request, "\r\n\r\n");
            if (header_end) {
                headers_complete = 1;
                headers_length = header_end - request + 4;
                
                // Look for Content-Length
                char* content_length_str = strstr(request, "Content-Length:");
                if (content_length_str) {
                    int content_length = atoi(content_length_str + strlen("Content-Length:"));
                    expected_total = headers_length + content_length;
                    printf("Expected total request size: %zu bytes (headers: %zu, body: %d)\n", 
                           expected_total, headers_length, content_length);
                } else {
                    // No content-length, so headers are the end
                    expected_total = total_received;
                }
            }
        }
        
        // Check if we have received the complete request
        if (headers_complete && total_received >= expected_total) {
            break;
        }
        
        // Resize buffer if needed
        if (total_received >= buffer_size - 1) {
            size_t new_buffer_size = buffer_size * 2;
            // Make sure new size can accommodate expected total
            if (expected_total > new_buffer_size) {
                new_buffer_size = expected_total + 1024;
            }
            
            request = reallocate_buffer(request, buffer_size, new_buffer_size);
            if (!request) return NULL;
            buffer_size = new_buffer_size;
        }
    }
    
    if (bytes_received < 0) {
        free(request);
        perror("Recv() error:");
        return NULL;
    }
    
    // Set the actual size
    if (request_size) {
        *request_size = total_received;
    }
    
    // Don't null-terminate at the end for binary data safety
    printf("Read complete request: %zu bytes\n", total_received);
    return request;
}


/**
 * @brief Parses the HTTP request line.
 * @param request The full HTTP request string.
 * @return A pointer to a new httpreq struct, or NULL on parsing failure.
 */
httpreq *parse_http(const char *request) {
    httpreq *req = malloc(sizeof(httpreq));
    if (!req) {
        return NULL;
    }

    char method[16], url[256], protocol[16];
    if (sscanf(request, "%15s %255s %15s", method, url, protocol) != 3) {
        free(req);
        return NULL;
    }

    req->method = strdup(method);
    req->url = strdup(url);
    req->protocol = strdup(protocol);

    if (!req->method || !req->url || !req->protocol) {
        free(req->method);
        free(req->url);
        free(req->protocol);
        free(req);
        return NULL;
    }

    return req;
}

/**
 * @brief Sends an HTTP response to a client.
 * @param client_socket The client socket.
 * @param status_code The HTTP status code.
 * @param content_type The content type of the response body.
 * @param body The response body content.
 * @param body_len The length of the response body.
 */
void http_send_response(int client_socket, int status_code, const char *content_type, const char *body, size_t body_len) {
    char header_buffer[1024];
    const char *status_message;
    switch (status_code) {
        case 200: status_message = "OK"; break;
        case 302: status_message = "Found"; break;
        case 400: status_message = "Bad Request"; break;
        case 401: status_message = "Unauthorized"; break;
        case 404: status_message = "Not Found"; break;
        case 500: status_message = "Internal Server Error"; break;
        default: status_message = "OK"; break;
    }
    
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_message, content_type, body_len);
             
    send(client_socket, header_buffer, strlen(header_buffer), 0);
    send(client_socket, body, body_len, 0);
    
}

/**
 * @brief Sends a simple redirect response.
 * @param client_socket The client socket.
 * @param location The URL to redirect to.
 */
void http_send_redirect(int client_socket, const char *location) {
    char header_buffer[1024];
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 302 Found\r\n"
             "Location: %s\r\n"
             "Connection: close\r\n"
             "\r\n", location);
    send(client_socket, header_buffer, strlen(header_buffer), 0);
}

/**
 * @brief Sends a redirect response with a session cookie.
 * @param client_socket The client socket.
 * @param location The URL to redirect to.
 * @param session_id The session ID to set in the cookie.
 */
void http_send_redirect_with_cookie(int client_socket, const char *location, const char *session_id) {
    char header_buffer[1024];
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 302 Found\r\n"
             "Location: %s\r\n"
             "Set-Cookie: session_id=%s; Max-Age=3600; Path=/\r\n"
             "Connection: close\r\n"
             "\r\n", location, session_id);
    send(client_socket, header_buffer, strlen(header_buffer), 0);
}

/**
 * @brief Gets the session ID from the HTTP request headers.
 * @param request The full HTTP request string.
 * @return The session ID string, or NULL if not found.
 */
const char *get_session_id_from_request(const char *request) {
    const char *cookie_header = find_header(request, "Cookie:");
    if (!cookie_header) {
        return NULL;
    }
    const char *session_id_start = strstr(cookie_header, "session_id=");
    if (!session_id_start) {
        return NULL;
    }
    session_id_start += strlen("session_id=");
    
    static char session_id[37];
    int i = 0;
    while (i < 36 && session_id_start[i] && session_id_start[i] != ';' && session_id_start[i] != ' ' && session_id_start[i] != '\r' && session_id_start[i] != '\n') {
        session_id[i] = session_id_start[i];
        i++;
    }
    session_id[i] = '\0';
    return session_id;
}

/**
 * @brief Decodes a URL-encoded string.
 * @param dest The destination buffer for the decoded string.
 * @param src The source URL-encoded string.
 */
void urldecode(char *dest, const char *src) {
    char hex[3];
    int i = 0, j = 0;
    while (src[i] && j < 255) {
        if (src[i] == '%') {
            hex[0] = src[i + 1];
            hex[1] = src[i + 2];
            hex[2] = '\0';
            dest[j] = strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dest[j] = ' ';
            i++;
        } else {
            dest[j] = src[i];
            i++;
        }
        j++;
    }
    dest[j] = '\0';
}


/**
 * @brief Sends the dashboard page to the client.
 * @param client_socket The client socket.
 * @param username The username to display.
 * @param path The current path within the user's directory.
 */
void http_send_dashboard(int client_socket, const char *username, const char *path) {
    char body[8192];
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "user_files/%s%s", username, path);

    char file_list_html[4096] = "";
    
    DIR *dir = opendir(full_path);
    if (dir) {
        // Add a "Go Up" link if not in the root directory
        if (strcmp(path, "/") != 0) {
            char parent_path[256];
            strncpy(parent_path, path, sizeof(parent_path) - 1);
            parent_path[sizeof(parent_path) - 1] = '\0';
            char *last_slash = strrchr(parent_path, '/');
            if (last_slash) {
                *last_slash = '\0'; // Truncate the string to the parent directory
            }
            if (strcmp(parent_path, "") == 0) {
                strcpy(parent_path, "/");
            }
            
            char go_up_html[512];
            snprintf(go_up_html, sizeof(go_up_html),
                     "<div class='file-item'>"
                     "<a href='/dashboard?path=%s'>Go Back</a>"
                     "</div>", parent_path);
            strcat(file_list_html, go_up_html);
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                char temp_html[1664];
                
                // Check if the entry is a directory
                if (ent->d_type == DT_DIR) {
                    char new_path[512];
                    snprintf(new_path, sizeof(new_path), "%s%s/", path, ent->d_name);
                    snprintf(temp_html, sizeof(temp_html),
                             "<div class='file-item'>"
                             "<span class='file-icon folder-icon'>&#128193;</span>"
                             "<a href='/dashboard?path=%s' class='file-name'>%s/</a>"
                             "<div class='file-actions'>"
                             "<form action='/delete_folder' method='post' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to delete this folder?\");'>"
                             "<input type='hidden' name='foldername' value='%s'>"
                             "<input type='hidden' name='path' value='%s'>"
                             "<button type='submit' class='delete-btn'>Delete</button>"
                             "</form>"
                             "</div>"
                             "</div>", new_path, ent->d_name, ent->d_name, path);
                } else { // It's a regular file

const char *file_name = ent->d_name;
const char *php_ext = strstr(file_name, ".php");
if (php_ext && *(php_ext + 4) == '\0') {
    // It's a PHP file, generate a direct link.
    snprintf(temp_html, sizeof(temp_html),
             "<div class='file-item'>"
             "<span class='file-icon file-icon'>&#128196;</span>"
             "<span class='file-name'>%s</span>"
             "<div class='file-actions'>"
             // CORRECTED: 'path' already includes 'user_files/username/'
             "<a href ='/user_files/%s/%s' class='view-btn'>View</a>"
             "<a href='/download_file?file=%s%s' class='download-btn'>Download</a>"
             "<form action='/delete_file' method='post' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to delete this file?\");'>"
             "<input type='hidden' name='filename' value='%s'>"
             "<input type='hidden' name='path' value='%s'>"
             "<button type='submit' class='delete-btn'>Delete</button>"
             "</form>"
             "</div>"
             "</div>", ent->d_name, username, file_name, username, file_name, file_name, username);
} else {
    // It's a static file, use the /view_file handler with the existing logic.
    snprintf(temp_html, sizeof(temp_html),
             "<div class='file-item'>"
             "<span class='file-icon file-icon'>&#128196;</span>"
             "<span class='file-name'>%s</span>"
             "<div class='file-actions'>"
             "<a href='/view_file?file=%s%s' class='view-btn'>View</a>"
             "<a href='/download_file?file=%s%s' class='download-btn'>Download</a>"
             "<form action='/delete_file' method='post' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to delete this file?\");'>"
             "<input type='hidden' name='filename' value='%s'>"
             "<input type='hidden' name='path' value='%s'>"
             "<button type='submit' class='delete-btn'>Delete</button>"
             "</form>"
             "</div>"
             "</div>", ent->d_name, path, ent->d_name, path, ent->d_name, ent->d_name, path);
}

                }
                strcat(file_list_html, temp_html);
            }
        }
        closedir(dir);
    } else {
        strcat(file_list_html, "<p>No files or folders in this directory.</p>");
    }

    snprintf(body, sizeof(body),
             "<!DOCTYPE html>"
             "<html lang='en'>"
             "<head>"
             "    <meta charset='UTF-8'>"
             "    <title>User Dashboard</title>"
             "    <style>"
             "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; color: #333; margin: 0; padding: 0; }"
             "        .container { max-width: 800px; margin: 40px auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); }"
             "        h1 { color: #1a73e8; }"
             "        h2 { border-bottom: 1px solid #eee; padding-bottom: 5px; }"
             "        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"
             "        .file-list { border-top: 1px solid #eee; padding-top: 20px; background_color:green;}"
             "        .file-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #eee; }"
             "        .file-name { font-weight: bold; flex-grow: 1; text-decoration: none; color: #333; }"
             "        .file-icon { font-size: 24px; margin-right: 10px; }"
             "        .file-actions a, .file-actions button { padding: 5px 10px; margin-left: 10px; border: none; border-radius: 4px; cursor: pointer; color: #fff; text-decoration: none; }"
             "        .view-btn, .download-btn { background-color: #4CAF50; }"
             "        .download-btn { background-color: #1a73e8; }"
             "        .delete-btn { background-color: #f44336; }"
             "        .upload-form, .create-folder-form { margin-top: 20px; padding: 20px; background-color: #f9f9f9; border-radius: 8px; }"
             "        input[type='file'], input[type='text'] { margin-bottom: 10px; padding: 8px; border: 1px solid #ccc; border-radius: 4px; width: calc(400px - 100px); }"
             "        .form-actions { text-align: right; }"
             "        .form-actions button { background-color: #1a73e8; color: #fff; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
             "        .logout-form { display: inline-block; }"
             "        .logout-form button { background-color: #f44336; color: #fff; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
             "    </style>"
             "</head>"
             "<body>"
             "    <div class='container'>"
             "        <div class='header'>"
             "            <h1>Welcome, %s!</h1>"
             "            <form class='logout-form' action='/logout' method='post'>"
             "                <button type='submit'>Logout</button>"
             "            </form>"
             "        </div>"
             "        <h2>Current Path: %s</h2>"
             "        <div class='file-list'>%s</div>"
             "        <div class='upload-form'>"
             "            <h2>Upload a New File</h2>"
             "            <form action='/upload' method='post' enctype='multipart/form-data'>"
             "                <input type='hidden' name='path' value='%s'>"
             "                <input type='file' name='file' required>"
             "                <div class='form-actions'>"
             "                    <button type='submit'>Upload File</button>"
             "                </div>"
             "            </form>"
             "        </div>"
             "        <div class='create-folder-form'>"
             "            <h2>Create New Folder</h2>"
             "            <form action='/create_folder' method='post'>"
             "                <input type='hidden' name='path' value='%s'>"
             "                <input type='text' name='foldername' placeholder='Folder Name' required>"
             "                <button type='submit'>Create Folder</button>"
             "            </form>"
             "        </div>"
             "    </div>"
             "</body>"
             "</html>", username, path, file_list_html, path, path);

    http_send_response(client_socket, 200, "text/html", body, strlen(body));
}



/**
 * @brief Sends the login page to the client.
 * @param client_socket The client socket.
 */
void http_send_login_page(int client_socket) {
    const char *body =
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <title>Login</title>"
        "    <style>"
        "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }"
        "        .login-container { background-color: #fff; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); width: 300px; text-align: center; }"
        "        h1 { color: #1a73e8; margin-bottom: 20px; }"
        "        input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }"
        "        button { width: 100%; padding: 10px; background-color: #1a73e8; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }"
        "        button:hover { background-color: #155bb5; }"
        "        .link-text { margin-top: 15px; font-size: 0.9em; }"
        "        .link-text a { color: #1a73e8; text-decoration: none; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='login-container'>"
        "        <h1>Login</h1>"
        "        <form action='/login' method='post'>"
        "            <input type='text' name='username' placeholder='Username' required>"
        "            <input type='password' name='password' placeholder='Password' required>"
        "            <button type='submit'>Login</button>"
        "        </form>"
        "        <p class='link-text'>Don't have an account? <a href='/register'>Register here</a></p>"
        "    </div>"
        "</body>"
        "</html>";
    http_send_response(client_socket, 200, "text/html", body, strlen(body));
}

/**
 * @brief Sends the registration page to the client.
 * @param client_socket The client socket.
 */
void http_send_register_page(int client_socket) {
    const char *body =
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <title>Register</title>"
        "    <style>"
        "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }"
        "        .register-container { background-color: #fff; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); width: 300px; text-align: center; }"
        "        h1 { color: #1a73e8; margin-bottom: 20px; }"
        "        input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }"
        "        button { width: 100%; padding: 10px; background-color: #1a73e8; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }"
        "        button:hover { background-color: #155bb5; }"
        "        .link-text { margin-top: 15px; font-size: 0.9em; }"
        "        .link-text a { color: #1a73e8; text-decoration: none; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='register-container'>"
        "        <h1>Register</h1>"
        "        <form action='/register' method='post'>"
        "            <input type='text' name='username' placeholder='Username' required>"
        "            <input type='password' name='password' placeholder='Password' required>"
        "            <button type='submit'>Register</button>"
        "        </form>"
        "        <p class='link-text'>Already have an account? <a href='/login'>Login here</a></p>"
        "    </div>"
        "</body>"
        "</html>";
    http_send_response(client_socket, 200, "text/html", body, strlen(body));
}


/**
 * @brief Sends the welcome page to the client.
 * @param client_socket The client socket.
 */
void http_send_welcome_page(int client_socket) {
    const char *body =
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <title>Welcome</title>"
        "    <style>"
        "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; margin: 0; }"
        "        .welcome-container { text-align: center; background-color: #fff; padding: 50px; border-radius: 12px; box-shadow: 0 6px 10px rgba(0, 0, 0, 0.15); }"
        "        h1 { color: #1a73e8; font-size: 2.5em; margin-bottom: 10px; }"
        "        p { color: #666; font-size: 1.1em; margin-bottom: 30px; }"
        "        .btn-group a { display: inline-block; padding: 12px 24px; text-decoration: none; border-radius: 8px; font-weight: bold; transition: background-color 0.3s ease; margin: 0 10px; }"
        "        .btn-login { background-color: #1a73e8; color: #fff; }"
        "        .btn-login:hover { background-color: #155bb5; }"
        "        .btn-register { background-color: #4CAF50; color: #fff; }"
        "        .btn-register:hover { background-color: #45a049; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='welcome-container'>"
        "        <h1>Welcome to Web server</h1>"
        "        <p>Your secure and simple file management solution.</p>"
        "        <p>We provide Support for HTTP and HTTPS and Server Side Scripting.</p>"
        "        <div class='btn-group'>"
        "            <a href='/login' class='btn-login'>Login</a>"
        "            <a href='/register' class='btn-register'>Register</a>"
        "        </div>"
        "    </div>"
        "</body>"
        "</html>";
    http_send_response(client_socket, 200, "text/html", body, strlen(body));
}

// New helper function to create a directory and its parents
int create_full_path(const char *path, mode_t mode) {
    char *pp;
    char *sp;
    int status;
    char *copypath = strdup(path);

    status = 0;
    pp = copypath;
    while (status == 0 && (sp = strchr(pp, '/')) != 0) {
        if (sp != pp) {
            *sp = '\0';
            if (mkdir(copypath, mode) != 0) {
                if (errno != EEXIST) {
                    status = -1;
                }
            }
            *sp = '/';
        }
        pp = sp + 1;
    }
    if (status == 0) {
        if (mkdir(path, mode) != 0) {
            if (errno != EEXIST) {
                status = -1;
            }
        }
    }
    free(copypath);
    return status;
}

void normalize_path(char *result, const char *base_path, const char *new_folder){
if(strcmp(new_folder,"..") == 0){
    //go up one directory 
    char *last_slash = strrchr(base_path,'/');
    if(last_slash && last_slash != base_path){
     strncpy(result,base_path, last_slash - base_path);
     result[last_slash - base_path] = '\0';
    }else{
        // If it's the root, stay at the root.
        strcpy(result, "/");
    }
}else{
    if(strcmp(base_path,"/")){
       // If the base is root, don't add an extra slash.
       snprintf(result,512,"/%s",new_folder);
    }else{
      // If the base is root, don't add an extra slash.
       snprintf(result,512,"%s/%s",base_path,new_folder);
    }
}
}


// Helper function to create directories recursively
int create_directory_recursive(const char *path) {
    char temp[MAX_PATH_LEN];
    char *p = NULL;
    size_t len;
    
    snprintf(temp, sizeof(temp), "%s", path);
    len = strlen(temp);
    
    if (temp[len - 1] == '/') {
        temp[len - 1] = 0;
    }
    
    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}
/**
 * @brief Handles a POST request to create a new folder.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */
bool handle_create_folder(const char *request, const char *username) {
    char foldername[256];
    char path[256] = "/";
    
    const char *body = strstr(request, "\r\n\r\n") + 4;
    
    if (get_post_param(body, "foldername", foldername, sizeof(foldername)) == 0) {
        fprintf(stderr, "Error: 'foldername' not found in POST data.\n");
        return false;
    }
    get_post_param(body, "path", path, sizeof(path));
    
    char decoded_path[256];
    urldecode(decoded_path, path);

    char full_path[640];
     //normalize_path(full_path,path,foldername);

      // Check if the current path is the root directory
    // if (strcmp(path, "/") == 0) {
    //     snprintf(full_path, sizeof(full_path), "user_files/%s/%s", username, foldername);
    // } else {
    //     // Concatenate with a forward slash for nested folders
    //     snprintf(full_path, sizeof(full_path), "user_files/%s%s/%s", username, path, foldername);
    // }
    snprintf(full_path, sizeof(full_path), "user_files/%s%s%s", username, decoded_path, foldername);

    // Use the new helper function instead of direct mkdir()
    if (create_full_path(full_path, 0777) != 0) {
        perror("Failed to create folder");
        return false;
    }

    return true;
}

// Helper function to find a needle in a haystack (like strstr but for binary data)
// Helper function to find a needle in a haystack (like strstr but for binary data)
// Binary-safe memory search function
const char* find_boundary(const char *haystack, size_t haystack_len,
                          const char *needle, size_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len) return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}


// Parse HTTP request into headers and body parts
http_request_parts_t* parse_http_request(const char *full_request) {
    const char *header_end = strstr(full_request, "\r\n\r\n");
    if (!header_end) {
        printf("Error: Could not find end of HTTP headers\n");
        return NULL;
    }
    
    http_request_parts_t *parts = malloc(sizeof(http_request_parts_t));
    if (!parts) {
        printf("Error: Memory allocation failed\n");
        return NULL;
    }
    
    // Calculate header length (including the \r\n\r\n)
    parts->headers_length = header_end - full_request + 4;
    
    // Allocate and copy headers
    parts->headers = malloc(parts->headers_length + 1);
    if (!parts->headers) {
        free(parts);
        return NULL;
    }
    strncpy(parts->headers, full_request, parts->headers_length);
    parts->headers[parts->headers_length] = '\0';
    
    // Calculate body length and position
    const char *body_start = header_end + 4;
    
    // Get Content-Length from headers to determine actual body size
    const char *content_length_line = strstr(parts->headers, "Content-Length:");
    if (content_length_line) {
        parts->body_length = atoi(content_length_line + 15); // Skip "Content-Length:"
    } else {
        // If no Content-Length, calculate from remaining data
        parts->body_length = strlen(full_request) - parts->headers_length;
    }
    
    printf("Parsed request: headers=%zu bytes, body=%zu bytes\n", 
           parts->headers_length, parts->body_length);
    
    // Important: For binary data, we can't use strlen() or string functions
    // We need to work with exact byte counts
    parts->body = (char*)body_start; // Point directly to body in original request
    
    return parts;
}

// Free the parsed request parts (only frees the structure and headers, not body)
void free_http_request_parts(http_request_parts_t *parts) {
    if (parts) {
        if (parts->headers) {
            free(parts->headers);
        }
        // Don't free body since it points to original request
        free(parts);
    }
}

// Enhanced function to find header value (more robust)
const char* find_header_value(const char *headers, const char *header_name) {
    const char *header_line = strstr(headers, header_name);
    if (!header_line) {
        return NULL;
    }
    
    // Skip the header name and any whitespace
    const char *value_start = header_line + strlen(header_name);
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    return value_start;
}

// Extract boundary from Content-Type header
char* extract_boundary(const char *content_type) {
    char *boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start) {
        return NULL;
    }
    
    boundary_start += 9; // Skip "boundary="
    
    // Remove quotes if present
    if (*boundary_start == '"') {
        boundary_start++;
    }
    
    char *boundary = malloc(MAX_BOUNDARY_LEN);
    int i = 0;
    while (boundary_start[i] && boundary_start[i] != '"' && 
           boundary_start[i] != ';' && boundary_start[i] != '\r' && 
           boundary_start[i] != '\n' && i < MAX_BOUNDARY_LEN - 1) {
        boundary[i] = boundary_start[i];
        i++;
    }
    boundary[i] = '\0';
    
    return boundary;
}

// Check if file type is binary based on extension
int is_binary_file(const char *filename) {
    const char *binary_extensions[] = {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".ico",
        ".mp4", ".avi", ".mov", ".wmv", ".flv", ".webm",
        ".mp3", ".wav", ".ogg", ".flac",
        ".zip", ".rar", ".7z", ".tar", ".gz",
        ".exe", ".dll", ".so", ".bin",
        NULL
    };
    
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    for (int i = 0; binary_extensions[i]; i++) {
        if (strcasecmp(ext, binary_extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Parse filename from Content-Disposition header
char* parse_filename(const char *disposition) {
    char *filename_start = strstr(disposition, "filename=\"");
    if (!filename_start) {
        return NULL;
    }
    
    filename_start += 10; // Skip 'filename="'
    char *filename_end = strchr(filename_start, '"');
    if (!filename_end) {
        return NULL;
    }
    
    size_t filename_len = filename_end - filename_start;
    char *filename = malloc(filename_len + 1);
    strncpy(filename, filename_start, filename_len);
    filename[filename_len] = '\0';
    
    return filename;
}

// Updated main file upload handler - works with your existing code
int handle_file_upload(const char *full_request, const char *username) {
    printf("Starting file upload for user: %s\n", username);
    
    // Parse the request into headers and body
    http_request_parts_t *parts = parse_http_request(full_request);
    if (!parts) {
        printf("Error: Failed to parse HTTP request\n");
        return -1;
    }
    
    // Get Content-Type header
    const char *content_type = find_header_value(parts->headers, "Content-Type:");
    if (!content_type) {
        printf("Error: No Content-Type header found\n");
        free_http_request_parts(parts);
        return -1;
    }
    
    // Check if it's multipart/form-data
    if (!strstr(content_type, "multipart/form-data")) {
        printf("Error: Content-Type is not multipart/form-data\n");
        free_http_request_parts(parts);
        return -1;
    }
    
    // Extract boundary
    char *boundary = extract_boundary(content_type);
    if (!boundary) {
        printf("Error: Could not extract boundary from Content-Type\n");
        free_http_request_parts(parts);
        return -1;
    }
    
    printf("Boundary: %s\n", boundary);
    printf("Processing body of %zu bytes\n", parts->body_length);
    
    // Create full boundary markers
    char start_boundary[MAX_BOUNDARY_LEN + 10];
    char end_boundary[MAX_BOUNDARY_LEN + 10];
    snprintf(start_boundary, sizeof(start_boundary), "--%s", boundary);
    snprintf(end_boundary, sizeof(end_boundary), "--%s--", boundary);
    
    // Parse the body for files and other form data
    const char *current = parts->body;
    const char *body_end = parts->body + parts->body_length;
    char upload_path[MAX_PATH_LEN] = ""; // Will be extracted from form data
    
    printf("Starting multipart parsing...\n");
    
    while (current < body_end) {
        // Find next boundary
        const char *boundary_pos = strstr(current, start_boundary);
        if (!boundary_pos || boundary_pos >= body_end) {
            break;
        }
        
        current = boundary_pos + strlen(start_boundary);
        
        // Skip CRLF after boundary
        if (current < body_end && *current == '\r') current++;
        if (current < body_end && *current == '\n') current++;
        
        // Parse headers for this part
        char content_disposition[512] = {0};
        char file_content_type[256] = "application/octet-stream";
        char form_name[256] = {0};
        
        // Read headers until empty line
        while (current < body_end) {
            const char *line_end = current;
            
            // Find end of line (handle both \r\n and \n)
            while (line_end < body_end && *line_end != '\r' && *line_end != '\n') {
                line_end++;
            }
            
            // Empty line marks end of headers
            if (line_end == current) {
                if (*current == '\r') current++;
                if (current < body_end && *current == '\n') current++;
                break;
            }
            
            // Parse Content-Disposition header
            if (strncasecmp(current, "Content-Disposition:", 20) == 0) {
                size_t header_len = line_end - current;
                if (header_len < sizeof(content_disposition)) {
                    strncpy(content_disposition, current, header_len);
                    content_disposition[header_len] = '\0';
                }
            }
            
            // Parse Content-Type header
            if (strncasecmp(current, "Content-Type:", 13) == 0) {
                const char *type_start = current + 13;
                while (*type_start == ' ') type_start++;
                size_t type_len = line_end - type_start;
                if (type_len < sizeof(file_content_type)) {
                    strncpy(file_content_type, type_start, type_len);
                    file_content_type[type_len] = '\0';
                }
            }
            
            // Move to next line
            current = line_end;
            if (current < body_end && *current == '\r') current++;
            if (current < body_end && *current == '\n') current++;
        }
        
        // Extract form field name
        char *name_start = strstr(content_disposition, "name=\"");
        if (name_start) {
            name_start += 6; // Skip 'name="'
            char *name_end = strchr(name_start, '"');
            if (name_end) {
                size_t name_len = name_end - name_start;
                if (name_len < sizeof(form_name)) {
                    strncpy(form_name, name_start, name_len);
                    form_name[name_len] = '\0';
                }
            }
        }
        
        // Find end of this part's data
        const char *next_boundary = body_end;
        const char *temp_boundary = strstr(current, start_boundary);
        if (temp_boundary && temp_boundary < body_end) {
            next_boundary = temp_boundary;
        }
        temp_boundary = strstr(current, end_boundary);
        if (temp_boundary && temp_boundary < next_boundary) {
            next_boundary = temp_boundary;
        }
        
        // Calculate data length (exclude trailing CRLF before boundary)
        size_t data_length = next_boundary - current;
        if (data_length >= 2 && next_boundary[-2] == '\r' && next_boundary[-1] == '\n') {
            data_length -= 2;
        } else if (data_length >= 1 && next_boundary[-1] == '\n') {
            data_length -= 1;
        }
        
        printf("Found form field: %s (length: %zu)\n", form_name, data_length);
        
        // Handle different form fields
        if (strcmp(form_name, "path") == 0) {
            // This is the path field
            if (data_length < sizeof(upload_path)) {
                strncpy(upload_path, current, data_length);
                upload_path[data_length] = '\0';
                printf("Upload path set to: '%s'\n", upload_path);
            }
        } else if (strstr(content_disposition, "filename=")) {
            // This is a file field
            char *filename = parse_filename(content_disposition);
            if (filename) {
                printf("Processing file: %s (Content-Type: %s, Size: %zu bytes)\n", 
                       filename, file_content_type, data_length);
                
                // Create full file path
                char full_path[1156];
                if (strlen(upload_path) > 0) {
                    snprintf(full_path, sizeof(full_path), "user_files/%s/%s/%s", 
                            username, upload_path, filename);
                } else {
                    snprintf(full_path, sizeof(full_path), "user_files/%s/%s", 
                            username, filename);
                }
                
                // Create directory if needed
                char dir_path[1156];
                if (strlen(upload_path) > 0) {
                    snprintf(dir_path, sizeof(dir_path), "user_files/%s/%s", 
                            username, upload_path);
                } else {
                    snprintf(dir_path, sizeof(dir_path), "user_files/%s", username);
                }
                create_directory_recursive(dir_path);
                //handle_create_folder()
                // Save file
                int binary_mode = is_binary_file(filename);
                FILE *file = fopen(full_path, binary_mode ? "wb" : "w");
                if (file) {
                    size_t written = fwrite(current, 1, data_length, file);
                    fclose(file);
                    
                    if (written == data_length) {
                        printf("Successfully saved file: %s (%zu bytes)\n", filename, written);
                    } else {
                        printf("Error: Partial write for file %s\n", filename);
                    }
                } else {
                    printf("Error: Could not create file %s: %s\n", full_path, strerror(errno));
                }
                
                free(filename);
            }
        }
        
        // Move to next part
        current = next_boundary;
    }
    
    free(boundary);
    free_http_request_parts(parts);
    printf("File upload processing completed\n");
    return 0;
}

int http_handle_upload(const char *full_request, const char *username) {
       int result = handle_file_upload(full_request, username);
    // Convert: 0 (success) -> 1, -1 (failure) -> 0
    return (result == 0) ? 1 : 0;
}
     

/**
 * @brief Handles a POST request to delete a file.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */
bool http_handle_delete_file(const char *request, const char *username) {
    pthread_mutex_lock(&file_mutex);
    char filename[256];
    char path[256] = "/";
    const char *body = strstr(request, "\r\n\r\n") + 4;
    
    // Get filename and path from POST body
    if (get_post_param(body, "filename", filename, sizeof(filename)) &&
        get_post_param(body, "path", path, sizeof(path))) {
        
        // Ensure path is properly decoded before use
        char decoded_path[256];
        urldecode(decoded_path, path);

        char filepath[640];
        // Correctly join the base user directory, the decoded path, and the filename.
        snprintf(filepath, sizeof(filepath), "user_files/%s%s%s", username, decoded_path, filename);
        
        printf("Attempting to delete file: %s\n", filepath);
        
        if (remove(filepath) == 0) {
            printf("Successfully deleted file.\n");
            return true;
        } else {
            perror("Failed to delete file");
        }
    }
    return false;
    pthread_mutex_unlock(&file_mutex);
    
}



/**
 * @brief Handles a POST request to delete a folder.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */
bool http_handle_delete_folder(const char *request,const char *username){
    
    char foldername[256];
    char path[256] = "/";
    const char *body = strstr(request, "\r\n\r\n") + 4;

    if (get_post_param(body, "foldername", foldername, sizeof(foldername)) && 
    get_post_param(body, "path", path, sizeof(path))) {

    // URL-decode the path parameter
    char decoded_path[256];
    urldecode(decoded_path, path);
    char folderpath[640];
    // Correct the typo: "users_files" -> "user_files"
    snprintf(folderpath, sizeof(folderpath), "user_files/%s%s%s", username, decoded_path, foldername);
    

    if (rmdir(folderpath) == 0) {
            printf("Folder deleted successfully: %s \n", folderpath);
            fflush(stdout);
            return true;
        } else {
            perror("Failed to delete Folder");
        }
   }
   return false; 
}

// A helper function to check for specific file extensions.
bool has_file_extension(const char *url, const char *ext) {
    const char *dot = strrchr(url, '.');
    if (!dot || dot == url) return false;
    return strcmp(dot, ext) == 0;
}
/**
 * @brief Handles a GET request to view a file.
 * @param client_socket The client socket.
 * @param url The request URL containing the filename.
 * @param username The username of the session.
 */
// In http_handler.c

void http_handle_view_file(int client_socket, const char *url, const char *username) {
    char decoded_path[256];
    const char *file_param = strstr(url, "file=");
    if (!file_param) {
        http_send_response(client_socket, 400, "text/plain", "Bad Request: Missing 'file' parameter", 37);
        return;
    }
    urldecode(decoded_path, file_param + 5);

   //new logic
    if (has_file_extension(url, ".php")) {
        // Block all attempts to view PHP source code
        http_send_response(client_socket, 403, "text/plain", "Forbidden", 9);
        return;
    }

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "user_files/%s%s", username, decoded_path);

    FILE *file = fopen(file_path, "r");
    if (!file) {
        http_send_response(client_socket, 404, "text/plain", "File not found.", 15);
        return;
    }

    // Determine the content type based on the file extension
    char *content_type = "text/plain"; // Default
    if (strstr(file_path, ".html")) {
        content_type = "text/html";
    } else if (strstr(file_path, ".css")) {
        content_type = "text/css";
    } else if (strstr(file_path, ".js")) {
        content_type = "application/javascript";
    } else if (strstr(file_path, ".jpg") || strstr(file_path, ".jpeg")) {
        content_type = "image/jpeg";
    } else if (strstr(file_path, ".png")) {
        content_type = "image/png";
    }
    // Add more file types as needed...

    // Send HTTP headers
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "\r\n",
             content_type);
    write(client_socket, header, strlen(header));

    // Send the file content
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        write(client_socket, buffer, bytes_read);
    }

    fclose(file);
}


/**
 * @brief Handles a GET request to download a file.
 * @param client_socket The client socket.
 * @param url The request URL containing the filename.
 * @param username The username of the session.
 */
void http_send_file_for_download(int client_socket, const char *url, const char *username) {
    char filename[256];
    const char *file_param = strstr(url, "file=");
    if (!file_param) {
        http_send_response(client_socket, 400, "text/plain", "Bad Request: Missing filename", 29);
        return;
    }
    file_param += strlen("file=");
    urldecode(filename, file_param);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "user_files/%s%s", username, filename);
    
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        http_send_response(client_socket, 404, "text/plain", "File Not Found", 14);
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *file_content = malloc(file_size);
    if (!file_content) {
        fclose(file);
        http_send_response(client_socket, 500, "text/plain", "Internal Server Error", 21);
        return;
    }
    
    fread(file_content, 1, file_size, file);
    fclose(file);
    
    char header_buffer[1024];
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/octet-stream\r\n"
             "Content-Disposition: attachment; filename=\"%s\"\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n", filename, file_size);
             
    send(client_socket, header_buffer, strlen(header_buffer), 0);
    send(client_socket, file_content, file_size, 0);
    
    free(file_content);
}

/**
 * @brief Helper function to get a parameter from a URL-encoded POST body.
 * @param body The URL-encoded string.
 * @param param_name The name of the parameter to find.
 * @param output The buffer to store the result.
 * @param output_size The size of the output buffer.
 * @return 1 on success, 0 on failure.
 */
int get_post_param(const char *body, const char *param_name, char *output, size_t output_size) {
    if (!body || !param_name || !output) {
        return 0;
    }

    char search_string[256];
    snprintf(search_string, sizeof(search_string), "%s=", param_name);
    
    const char *start = strstr(body, search_string);
    if (!start) {
        return 0;
    }
    start += strlen(search_string);
    
    const char *end = strchr(start, '&');
    size_t len;
    if (end) {
        len = end - start;
    } else {
        len = strlen(start);
    }
    
    if (len >= output_size) {
        return 0;
    }
    
    strncpy(output, start, len);
    output[len] = '\0';
    return 1;
}

/**
 * @brief Handles a multipart/form-data file upload request.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */


//  bool http_handle_upload(int client_socket, const char *full_request, const char *username, int content_length) {
//     printf("=== BULLETPROOF UPLOAD HANDLER ===\n");
    
//     // 1. First, save the complete request for debugging
//     FILE *debug = fopen("upload_debug.bin", "wb");
//     if (debug) {
//         fwrite(full_request, 1, strlen(full_request), debug);
//         fclose(debug);
//         printf("Saved complete request to upload_debug.bin\n");
//     }

//     // 2. Extract boundary
//     const char *content_type = find_header(full_request, "Content-Type:");
//     if (!content_type) {
//         printf("Error: No Content-Type header\n");
//         return false;
//     }
    
//     const char *boundary_start = strstr(content_type, "boundary=");
//     if (!boundary_start) {
//         printf("Error: No boundary in Content-Type\n");
//         return false;
//     }
//     boundary_start += strlen("boundary=");
    
//     char boundary[100];
//     const char *boundary_end = strpbrk(boundary_start, ";\r\n");
//     if (boundary_end) {
//         strncpy(boundary, boundary_start, boundary_end - boundary_start);
//         boundary[boundary_end - boundary_start] = '\0';
//     } else {
//         strncpy(boundary, boundary_start, sizeof(boundary) - 1);
//         boundary[sizeof(boundary) - 1] = '\0';
//     }
//     printf("Boundary: '%s'\n", boundary);

//     // 3. Extract filename using a more robust method
//     char filename[256] = {0};
//     const char *filename_ptr = full_request;
    
//     // Try multiple patterns to find filename
//     const char *patterns[] = {
//         "filename=\"",
//         "filename=",
//         "name=\"file\"; filename=\"",
//         NULL
//     };
    
//     for (int i = 0; patterns[i] != NULL; i++) {
//         filename_ptr = strstr(full_request, patterns[i]);
//         if (filename_ptr) {
//             filename_ptr += strlen(patterns[i]);
//             const char *filename_end = strchr(filename_ptr, '"');
//             if (filename_end) {
//                 strncpy(filename, filename_ptr, filename_end - filename_ptr);
//                 filename[filename_end - filename_ptr] = '\0';
//                 break;
//             }
//         }
//     }
    
//     if (strlen(filename) == 0) {
//         printf("Error: Could not extract filename\n");
//         return false;
//     }
//     printf("Filename: '%s'\n", filename);

//     // 4. Extract path
//     char path[256] = "/";
//     const char *path_ptr = strstr(full_request, "name=\"path\"");
//     if (path_ptr) {
//         path_ptr = strstr(path_ptr, "\r\n\r\n");
//         if (path_ptr) {
//             path_ptr += 4;
//             const char *path_end = strstr(path_ptr, "\r\n");
//             if (path_end) {
//                 strncpy(path, path_ptr, path_end - path_ptr);
//                 path[path_end - path_ptr] = '\0';
//             }
//         }
//     }
    
//     char decoded_path[256];
//     urldecode(decoded_path, path);
//     printf("Path: '%s'\n", decoded_path);

//     // 5. Create file path
//     char filepath[1024];
//     if (strcmp(decoded_path, "/") == 0) {
//         snprintf(filepath, sizeof(filepath), "user_files/%s/%s", username, filename);
//     } else {
//         snprintf(filepath, sizeof(filepath), "user_files/%s%s/%s", username, decoded_path, filename);
//     }
//     printf("Saving to: '%s'\n", filepath);

//     // 6. ROBUST CONTENT EXTRACTION - Try multiple methods
//     const char *file_content_start = NULL;
//     const char *file_content_end = NULL;
//     size_t file_size = 0;

//     // Method 1: Look for the file content after filename
//     const char *file_section = strstr(full_request, "filename=");
//     if (file_section) {
//         file_section = strstr(file_section, "\r\n\r\n");
//         if (file_section) {
//             file_content_start = file_section + 4;
//             printf("Found file content start via filename method\n");
//         }
//     }

//     // Method 2: Look for second occurrence of \r\n\r\n
//     if (!file_content_start) {
//         const char *first_newline = strstr(full_request, "\r\n\r\n");
//         if (first_newline) {
//             file_content_start = strstr(first_newline + 4, "\r\n\r\n");
//             if (file_content_start) {
//                 file_content_start += 4;
//                 printf("Found file content start via double newline method\n");
//             }
//         }
//     }

//     if (!file_content_start) {
//         printf("Error: Could not find file content start using any method\n");
//         return false;
//     }

//     // 7. Find the end of file content using multiple boundary patterns
//     const char *boundary_patterns[] = {
//         "\r\n--",      // Most common
//         "--",          // Alternative
//         "\n--",        // Unix line endings
//         NULL
//     };

//     for (int i = 0; boundary_patterns[i] != NULL && !file_content_end; i++) {
//         char end_pattern[150];
//         snprintf(end_pattern, sizeof(end_pattern), "%s%s", boundary_patterns[i], boundary);
//         file_content_end = strstr(file_content_start, end_pattern);
        
//         if (file_content_end) {
//             printf("Found content end with pattern: '%s'\n", end_pattern);
//             break;
//         }
//     }

//     if (!file_content_end) {
//         // Last resort: search for any boundary-like pattern
//         char simple_pattern[110];
//         snprintf(simple_pattern, sizeof(simple_pattern), "--%s", boundary);
//         file_content_end = strstr(file_content_start, simple_pattern);
        
//         if (file_content_end) {
//             printf("Found content end with simple pattern: '%s'\n", simple_pattern);
//         }
//     }

//     if (!file_content_end) {
//         printf("Error: Could not find file content end\n");
//         printf("Content starts with: ");
//         for (int i = 0; i < 50 && file_content_start[i] != '\0'; i++) {
//             if (isprint(file_content_start[i])) {
//                 printf("%c", file_content_start[i]);
//             } else {
//                 printf("\\x%02X", (unsigned char)file_content_start[i]);
//             }
//         }
//         printf("\n");
//         return false;
//     }

//     // 8. Calculate file size (remove trailing CRLF if present)
//     file_size = file_content_end - file_content_start;
    
//     // Check for trailing \r\n before boundary
//     if (file_size >= 2 && 
//         file_content_end[-2] == '\r' && 
//         file_content_end[-1] == '\n') {
//         file_size -= 2;
//         printf("Removed trailing \\r\\n\n");
//     }
//     // Check for trailing \n before boundary
//     else if (file_size >= 1 && file_content_end[-1] == '\n') {
//         file_size -= 1;
//         printf("Removed trailing \\n\n");
//     }

//     printf("File content size: %zu bytes\n", file_size);

//     // 9. Create directory and save file
//     char directory[512];
//     snprintf(directory, sizeof(directory), "user_files/%s%s", username, decoded_path);
//     create_full_path(directory, 0777);
    
//     FILE *file = fopen(filepath, "wb");
//     if (!file) {
//         perror("Failed to create file");
//         return false;
//     }
    
//     size_t written = fwrite(file_content_start, 1, file_size, file);
//     fclose(file);
    
//     printf("Written %zu bytes to %s\n", written, filepath);
    
//     // 10. Verify the file
//     if (written != file_size) {
//         printf("Error: File size mismatch (expected %zu, got %zu)\n", file_size, written);
//         return false;
//     }

//     // Read back to verify content
//     FILE *verify = fopen(filepath, "rb");
//     if (verify) {
//         char first_bytes[100];
//         size_t read = fread(first_bytes, 1, sizeof(first_bytes) - 1, verify);
//         first_bytes[read] = '\0';
//         fclose(verify);
        
//         printf("File starts with: ");
//         for (size_t i = 0; i < (read < 50 ? read : 50); i++) {
//             if (isprint(first_bytes[i])) {
//                 printf("%c", first_bytes[i]);
//             } else {
//                 printf("\\x%02X", (unsigned char)first_bytes[i]);
//             }
//         }
//         printf("\n");
//     }
    
//     printf("SUCCESS: File uploaded successfully!\n");
//     return true;
// }
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

// char* read_full_request(int client_socket, size_t *total_size_out) {
//     char *request = NULL;
//     size_t total_size = 0;
//     ssize_t bytes_received;
//     char buffer[4096];
//     int content_length = -1;
//     int headers_received = 0;

//     // Read headers first
//     while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
//         printf("Received %zd bytes in header phase\n", bytes_received);
        
//         char *new_request = realloc(request, total_size + bytes_received + 1);
//         if (!new_request) {
//             printf("Realloc failed at header phase\n");
//             free(request);
//             return NULL;
//         }
//         request = new_request;
        
//         memcpy(request + total_size, buffer, bytes_received);
//         total_size += bytes_received;
//         request[total_size] = '\0';

//         // Check for end of headers
//         char *header_end = strstr(request, "\r\n\r\n");
//         if (header_end) {
//             headers_received = 1;
            
//             // Extract Content-Length
//             char *cl_header = strstr(request, "Content-Length:");
//             if (cl_header) {
//                 content_length = atoi(cl_header + strlen("Content-Length:"));
//                 printf("Content-Length: %d\n", content_length);
//             }
//             break;
//         }
        
//         if (total_size > 16384) {
//             printf("Header size limit exceeded\n");
//             break;
//         }
//     }

//     if (!headers_received || bytes_received <= 0) {
//         printf("Failed to read headers\n");
//         free(request);
//         return NULL;
//     }

//     // Calculate total expected request size
//     char *body_start = strstr(request, "\r\n\r\n") + 4;
//     size_t headers_size = body_start - request;
//     size_t total_expected_size = headers_size + content_length;

//     printf("Headers size: %zu, Expected total: %zu, Current: %zu\n", 
//            headers_size, total_expected_size, total_size);

//     // Read remaining data
//     while (total_size < total_expected_size) {
//         size_t remaining = total_expected_size - total_size;
//         size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        
//         bytes_received = recv(client_socket, buffer, to_read, 0);
//         if (bytes_received <= 0) {
//             printf("Recv error or connection closed. Expected: %zu, Got: %zu\n", 
//                    total_expected_size, total_size);
//             break;
//         }
        
//         printf("Received %zd bytes in body phase\n", bytes_received);
        
//         char *new_request = realloc(request, total_size + bytes_received + 1);
//         if (!new_request) {
//             printf("Realloc failed for %zu bytes\n", total_size + bytes_received);
//             free(request);
//             return NULL;
//         }
//         request = new_request;
        
//         memcpy(request + total_size, buffer, bytes_received);
//         total_size += bytes_received;
        
//         printf("Progress: %zu/%zu bytes (%.1f%%)\n", 
//                total_size, total_expected_size,
//                ((double)total_size / total_expected_size) * 100);
//     }

//     request[total_size] = '\0';
    
//     if (total_size_out) {
//         *total_size_out = total_size;
//     }
    
//     printf("Final total request size: %zu bytes\n", total_size);
//     return request;
// }