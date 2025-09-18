#ifndef HTTPS_HANDLER_H
#define HTTPS_HANDLER_H

void https_send_welcome_page(void* client_socket);
void https_send_redirect(void* client_socket, const char *location);
void https_send_response(void* client_socket, int status_code, const char *content_type, const char *body, size_t body_len);


#endif