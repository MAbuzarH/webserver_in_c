#include <openssl/ssl.h>
#include <openssl/err.h>
#include "https_server.h"
#include "https_handler.h"
#include <stdio.h>

void https_send_welcome_page(void* client_sock) {
    SSL* client_socket = (SSL*) client_sock;
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
        
        //ssl_send_wrapper();
    https_send_response(client_socket, 200, "text/html", body, strlen(body));
}

void https_send_redirect(void* client_socket, const char *location) {
    char header_buffer[1024];
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 302 Found\r\n"
             "Location: %s\r\n"
             "Connection: close\r\n"
             "\r\n", location);
    ssl_send_wrapper(client_socket, header_buffer, strlen(header_buffer));
}

void https_send_response(void* client_soc, int status_code, const char *content_type, const char *body, size_t body_len) {
    SSL* client_socket = (SSL*)client_soc;
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
             
    //send(client_socket, header_buffer, strlen(header_buffer), 0);
    ssl_send_wrapper(client_socket, header_buffer, strlen(header_buffer));
    ssl_send_wrapper(client_socket, body, body_len);
}