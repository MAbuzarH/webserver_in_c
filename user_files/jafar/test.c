/**
 * Handles an incoming HTTP request from a client.
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
    char *cookie_header = strstr(request, "Cookie: ");
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
            char *user_part = strstr(body, "username=");
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
            char *file_path = user_end + 1;
             //how to point accros
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


------geckoformboundary9ddfdbf1a8d2d353b72a24bc90c5089--
