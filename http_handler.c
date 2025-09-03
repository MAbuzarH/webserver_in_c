// // http_handler.c
// // Implements HTTP request parsing and response sending logic.

#include "http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>

#define BUFFER_SIZE 8192
#define MAX_USERNAME_LENGTH 63

// Helper function to find a header in the request
const char *find_header(const char *request, const char *header_name) {
    const char *header_start = strstr(request, header_name);
    if (!header_start) {
        return NULL;
    }
    header_start += strlen(header_name);
    while (*header_start == ' ' || *header_start == ':') {
        header_start++;
    }
    return header_start;
}

/**
 * @brief Reads the entire HTTP request from a socket.
 * @param client_socket The client socket file descriptor.
 * @return A dynamically allocated string containing the full request, or NULL on error.
 */
char *read_full_request(int client_socket) {
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        return NULL;
    }

    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        free(buffer);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    return buffer;
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
 */
void http_send_dashboard(int client_socket, const char *username) {
    char body[8192];
    char user_files_path[256];
    snprintf(user_files_path, sizeof(user_files_path), "user_files/%s", username);

    char file_list_html[4096] = "";
    
     DIR *dir = opendir(user_files_path);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                // ADDED DEBUGGING PRINT STATEMENT HERE
                printf("Generating link for file: %s\n", ent->d_name);
                fflush(stdout);

                char temp_html[1664];
                snprintf(temp_html, sizeof(temp_html),
         "<div class='file-item'>"
         "<span class='file-name'>%s</span>"
         "<div class='file-actions'>"
         "<a href='/view_file?file=%s' class='view-btn'>View</a>"
         "<a href='/download_file?file=%s' class='download-btn'>Download</a>"
         "<form action='/delete_file' method='post' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to delete this file?\");'>"
         "<input type='hidden' name='filename' value='%s'>"
         "<button type='submit'>Delete</button>"
         "</form>"
         "</div>"
         "</div>", ent->d_name, ent->d_name, ent->d_name, ent->d_name);
                strcat(file_list_html, temp_html);
            }
        }
        closedir(dir);
    } else {
        strcat(file_list_html, "<p>No files uploaded yet.</p>");
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
             "        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"
             "        .file-list { border-top: 1px solid #eee; padding-top: 20px; }"
             "        .file-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #eee; }"
             "        .file-name { font-weight: bold; flex-grow: 1; }"
             "        .file-actions button { padding: 5px 10px; margin-left: 10px; border: none; border-radius: 4px; cursor: pointer; color: #fff; }"
             "        .file-actions button[type='submit'] { background-color: #4CAF50; }"
             "        .file-actions button:last-child { background-color: #f44336; }"
             "        .upload-form { margin-top: 20px; padding: 20px; background-color: #f9f9f9; border-radius: 8px; }"
             "        input[type='file'] { margin-bottom: 10px; }"
             "        .form-actions { text-align: right; }"
             "        .form-actions button { background-color: #1a73e8; color: #fff; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }"
             "        .logout-form { display: inline-block; }"
             "        .logout-form button { background-color: #f44336; color: #fff; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; }"
             "        .view-btn, .download-btn {"
"            display: inline-block;"
"            padding: 5px 10px;"
"            margin-left: 10px;"
"            border: none;"
"            border-radius: 4px;"
"            cursor: pointer;"
"            color: #fff;"
"            text-decoration: none;"
"            text-align: center;"
"            line-height: 1.5;" 
"        }"
"        .view-btn { background-color: #4CAF50; }"
"        .download-btn { background-color: #1a73e8; }"
"        .delete-btn { background-color: #f44336; }"
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
             "        <h2>Your Files</h2>"
             "        <div class='file-list'>%s</div>"
             "        <div class='upload-form'>"
             "            <h2>Upload a New File</h2>"
             "            <form action='/upload' method='post' enctype='multipart/form-data'>"
             "                <input type='file' name='file' required>"
             "                <div class='form-actions'>"
             "                    <button type='submit'>Upload File</button>"
             "                </div>"
             "            </form>"
             "        </div>"
             "    </div>"
             "</body>"
             "</html>", username, file_list_html);

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
        "        <h1>Welcome to File Manager</h1>"
        "        <p>Your secure and simple file management solution.</p>"
        "        <div class='btn-group'>"
        "            <a href='/login' class='btn-login'>Login</a>"
        "            <a href='/register' class='btn-register'>Register</a>"
        "        </div>"
        "    </div>"
        "</body>"
        "</html>";
    http_send_response(client_socket, 200, "text/html", body, strlen(body));
}

/**
 * @brief Handles a multipart/form-data file upload request.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */
bool http_handle_upload(const char *request, const char *username) {
    // Find boundary string
    const char *boundary_start = strstr(request, "boundary=");
    if (!boundary_start) return false;
    boundary_start += strlen("boundary=");
    const char *boundary_end = strstr(boundary_start, "\r\n");
    if (!boundary_end) return false;
    size_t boundary_len = boundary_end - boundary_start;
    
    char boundary[boundary_len + 3];
    snprintf(boundary, sizeof(boundary), "--%.*s", (int)boundary_len, boundary_start);
    
    // Find filename
    const char *filename_start = strstr(request, "filename=\"");
    if (!filename_start) return false;
    filename_start += strlen("filename=\"");
    const char *filename_end = strchr(filename_start, '"');
    if (!filename_end) return false;
    size_t filename_len = filename_end - filename_start;
    
    char filename[256];
    if (filename_len >= sizeof(filename)) {
        return false;
    }
    strncpy(filename, filename_start, filename_len);
    filename[filename_len] = '\0';
    
    // Find the body start
    const char *body_start = strstr(filename_end, "\r\n\r\n");
    if (!body_start) return false;
    body_start += 4;
    
    // Find body end
    char end_boundary[boundary_len + 5];
    snprintf(end_boundary, sizeof(end_boundary), "\r\n%s--", boundary);
    const char *body_end = strstr(body_start, end_boundary);
    if (!body_end) return false;
    size_t content_len = body_end - body_start;
    
    // Ensure the user directory exists
    char user_path[256];
    snprintf(user_path, sizeof(user_path), "user_files/%s", username);
    mkdir(user_path, 0777);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", user_path, filename);
    
    FILE *file = fopen(filepath, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        return false;
    }
    
    fwrite(body_start, 1, content_len, file);
    fclose(file);
    
    return true;
}

/**
 * @brief Handles a POST request to delete a file.
 * @param request The full HTTP request string.
 * @param username The username of the session.
 * @return true on success, false on failure.
 */
bool http_handle_delete_file(const char *request, const char *username) {
    char filename[256];
    const char *body = strstr(request, "\r\n\r\n") + 4;
    
    if (get_post_param(body, "filename", filename, sizeof(filename))) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "user_files/%s/%s", username, filename);
        
        printf("Attempting to delete file: %s\n", filepath);
        fflush(stdout);
        
        if (remove(filepath) == 0) {
            printf("Successfully deleted file.\n");
            return true;
        } else {
            perror("Failed to delete file");
        }
    }
    return false;
}

/**
 * @brief Handles a GET request to view a file.
 * @param client_socket The client socket.
 * @param url The request URL containing the filename.
 * @param username The username of the session.
 */
void http_handle_view_file(int client_socket, const char *url, const char *username) {
    char filename[256];
    const char *file_param = strstr(url, "file=");
    printf("file name in view: %s \n",file_param);
     fflush(stdout);
    if (!file_param) {
        http_send_response(client_socket, 400, "text/plain", "Bad Request: Missing filename", 29);
        return;
    }
    file_param += strlen("file=");
    urldecode(filename, file_param);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "user_files/%s/%s", username, filename);
    
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
    
    char response_body[file_size + 1024];
    snprintf(response_body, sizeof(response_body),
             "<!DOCTYPE html>"
             "<html lang='en'>"
             "<head>"
             "    <meta charset='UTF-8'>"
             "    <title>View File: %s</title>"
             "    <style>"
             "        body { font-family: sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; }"
             "        .container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); }"
             "        pre { background-color: #f9f9f9; padding: 15px; border-radius: 4px; overflow: auto; white-space: pre-wrap; word-wrap: break-word; }"
             "        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"
             "        .back-btn { padding: 8px 16px; background-color: #1a73e8; color: #fff; text-decoration: none; border-radius: 4px; }"
             "    </style>"
             "</head>"
             "<body>"
             "    <div class='container'>"
             "        <div class='header'>"
             "            <h1>Viewing File: %s</h1>"
             "            <a href='/dashboard' class='back-btn'>Back to Dashboard</a>"
             "        </div>"
             "        <pre>%s</pre>"
             "    </div>"
             "</body>"
             "</html>", filename, filename, file_content);
             
    http_send_response(client_socket, 200, "text/html", response_body, strlen(response_body));
    free(file_content);
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
    snprintf(filepath, sizeof(filepath), "files/%s/%s", username, filename);
    
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
