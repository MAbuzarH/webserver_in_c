// stress_test_users.c
// Multi-user, multithreaded stress tester that registers, logs in, and uploads files.
//
// Compile:
//   gcc test2.c -o test2.c -lpthread
//
// Run (example):
//   ./test2 127.0.0.1 8080 20 2
//
// Arguments:
//   argv[1] = server IP (default 127.0.0.1)
//   argv[2] = server port (default 8080)
//   argv[3] = number of users (default 20)
//   argv[4] = uploads per user (default 2)

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>

#define BUF_SIZE 16384
#define COOKIE_MAX 512

static const char *DEFAULT_USERNAME_PREFIX = "stress_user";
static const char *DEFAULT_PASSWORD_PREFIX = "P@ssw0rd";

typedef struct {
    char server_ip[64];
    int port;
    int user_index;        // unique id starting from 1
    int uploads_per_user;
} worker_arg_t;

static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
static int total_pass = 0;
static int total_fail = 0;

// Helpers --------------------------------------------------------------------

static int set_socket_timeout(int sock, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

static int connect_to_server(const char *ip, int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) <= 0) {
        close(s);
        return -1;
    }
    if (connect(s, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        close(s);
        return -1;
    }
    // reasonable timeouts so tests don't hang indefinitely
    set_socket_timeout(s, 8);
    return s;
}

// send all bytes
static int send_all(int sock, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, buf + sent, (int)(len - sent), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

// read headers until CRLFCRLF or up to maxlen-1 and put into buffer (null-terminated).
// returns header length in bytes, or -1 on error.
static ssize_t recv_headers(int sock, char *buf, size_t maxlen) {
    size_t total = 0;
    while (total + 4 < maxlen) {
        ssize_t n = recv(sock, buf + total, (int)(maxlen - total - 1), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
        buf[total] = '\0';
        if (total >= 4) {
            // search for "\r\n\r\n"
            for (size_t i = 0; i + 3 < total; ++i) {
                if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                    return (ssize_t)(i + 4);
                }
            }
        }
        // continue reading until header end
    }
    return -1;
}

// parse HTTP status code from response header buffer (expects null-terminated start)
static int parse_status_code(const char *headers) {
    // expects "HTTP/1.1 <code> ..."
    const char *p = strstr(headers, " ");
    if (!p) return -1;
    int code = atoi(p + 1);
    if (code <= 0) return -1;
    return code;
}

// extract first Set-Cookie header value up to semicolon (e.g., "sessionid=abc; Path=/;...")
// writes cookie_str (size cookie_len), returns 1 if found, 0 otherwise.
static int extract_set_cookie(const char *headers, char *cookie_str, size_t cookie_len) {
    const char *p = strstr(headers, "Set-Cookie:");
    if (!p) return 0;
    p += strlen("Set-Cookie:");
    while (*p == ' ' || *p == '\t') ++p;
    const char *end = strchr(p, ';');
    if (!end) {
        // try to end at CRLF
        end = strstr(p, "\r\n");
        if (!end) return 0;
    }
    size_t len = (size_t)(end - p);
    if (len >= cookie_len) len = cookie_len - 1;
    memcpy(cookie_str, p, len);
    cookie_str[len] = '\0';
    return 1;
}

// Check status success: treat 200,201,302,303 as success for our flows.
static int status_is_success(int code) {
    return (code == 200 || code == 201 || code == 302 || code == 303 || code == 204);
}

// form-encode a string (very simple, only encodes space and few chars). For this tester names are safe.
static void url_encode_simple(const char *src, char *dst, size_t dst_len) {
    // simple copy — our usernames/passwords are safe ASCII
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

// worker actions ------------------------------------------------------------

// Register user: POST /register with application/x-www-form-urlencoded
static int do_register(const char *ip, int port, const char *username, const char *password) {
    int sock = connect_to_server(ip, port);
    if (sock < 0) return 0;

    char body[1024];
    char ue[256], pe[256];
    url_encode_simple(username, ue, sizeof(ue));
    url_encode_simple(password, pe, sizeof(pe));
    snprintf(body, sizeof(body), "username=%s&password=%s", ue, pe);

    char header[1024];
    snprintf(header, sizeof(header),
             "POST /register HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %zu\r\n\r\n",
             strlen(body));

    if (send_all(sock, header, strlen(header)) < 0 || send_all(sock, body, strlen(body)) < 0) {
        close(sock); return 0;
    }

    char hdrbuf[BUF_SIZE];
    ssize_t hlen = recv_headers(sock, hdrbuf, sizeof(hdrbuf));
    close(sock);
    if (hlen < 0) return 0;
    int code = parse_status_code(hdrbuf);
    return status_is_success(code);
}

// Login user: POST /login; capture cookie into cookie_out (must be large enough)
static int do_login_and_get_cookie(const char *ip, int port, const char *username, const char *password, char *cookie_out, size_t cookie_out_len) {
    int sock = connect_to_server(ip, port);
    if (sock < 0) return 0;

    char body[1024];
    char ue[256], pe[256];
    url_encode_simple(username, ue, sizeof(ue));
    url_encode_simple(password, pe, sizeof(pe));
    snprintf(body, sizeof(body), "username=%s&password=%s", ue, pe);

    char header[1024];
    snprintf(header, sizeof(header),
             "POST /login HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %zu\r\n\r\n",
             strlen(body));

    if (send_all(sock, header, strlen(header)) < 0 || send_all(sock, body, strlen(body)) < 0) {
        close(sock); return 0;
    }

    char hdrbuf[BUF_SIZE];
    ssize_t hlen = recv_headers(sock, hdrbuf, sizeof(hdrbuf));
    close(sock);
    if (hlen < 0) return 0;
    int code = parse_status_code(hdrbuf);
    if (!status_is_success(code)) return 0;
    if (!extract_set_cookie(hdrbuf, cookie_out, cookie_out_len)) {
        // sometimes cookie may be in lowercase or different header; try scanning lines
        return 0;
    }
    return 1;
}

// Returns 1 on success, 0 on failure.
// static int do_delete_with_cookie(const char *ip, int port, const char *cookie, const char *username, int upload_index) {

//     int sock = connect_to_server(ip, port);
//     if (sock < 0) return 0;


//     char body[512];
//     snprintf(body, sizeof(body),
//     "filename=%s_upload_%d.txt", username, upload_index);

//     char header[1024];

//     snprintf(header,sizeof(header),
//         "POST /delete HTTP/1.1\r\n"
//         "Host: localhost\r\n"
//         "Cookie: %s\r\n"
//         "Content-Type: application/x-www-form-urlencoded\r\n"
//         "Content-Length: %zu\r\n\r\n",
//     cookie, strlen(body));

//     if(send_all(sock,header,sizeof(header)) < 0 || send_all(sock,body,sizeof(body)) < 0){
//       close(sock); 
//       return 0;
//     }
// // int ok =0;
//     char resp[BUF_SIZE];
//     size_t retl = recv_headers(sock,resp,sizeof(resp));
//     close(sock); 

//     if(retl < 0){
//        return 0;
//     }

//     int code = parse_status_code(resp);
//     return status_is_success(code);

// }

// Upload a small file (in-memory content) using multipart/form-data with Cookie header.
// Returns 1 on success, 0 on failure.
static int do_upload_with_cookie(const char *ip, int port, const char *cookie, const char *username, int upload_index) {
    int sock = connect_to_server(ip, port);
    if (sock < 0) return 0;

    // build a small binary payload (works for binary and text)
    char filedata[512];
    int filedata_len = snprintf(filedata, sizeof(filedata), "Hello from %s upload#%d\nRandom=%d\n", username, upload_index, rand());

    // boundary
    char boundary[64];
    snprintf(boundary, sizeof(boundary), "----STRESSBND%08X", rand() & 0xFFFFFF);

    // compose multipart body into heap because size is small
    char part_header[1024];
    char filename[128];
    snprintf(filename, sizeof(filename), "%s_upload_%d.txt", username, upload_index);

    int part_header_len = snprintf(part_header, sizeof(part_header),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n",
        boundary, filename);

    int part_footer_len = snprintf(NULL, 0, "\r\n--%s--\r\n", boundary); // we will format later

    size_t body_len = (size_t)part_header_len + (size_t)filedata_len + (size_t)part_footer_len + strlen(boundary) + 8;
    // allocate
    char *body = malloc(body_len + 64);
    if (!body) { close(sock); return 0; }
    size_t pos = 0;
    memcpy(body + pos, part_header, (size_t)part_header_len); pos += (size_t)part_header_len;
    memcpy(body + pos, filedata, (size_t)filedata_len); pos += (size_t)filedata_len;
    int n = snprintf(body + pos, 64 + part_footer_len, "\r\n--%s--\r\n", boundary);
    pos += (size_t)n;

    // build HTTP headers
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: session_id=%s\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %zu\r\n\r\n",
        cookie, boundary, pos);

    int ok = 0;
    if (send_all(sock, header, header_len) == 0 && send_all(sock, body, pos) == 0) {
        // read response header
        char resp[BUF_SIZE];
        ssize_t rlen = recv_headers(sock, resp, sizeof(resp));
        if (rlen > 0) {
            int code = parse_status_code(resp);
            ok = status_is_success(code);
        }
    }

    free(body);
    close(sock);
    return ok;
}

// Worker thread: register, login, then perform uploads
static void *worker_thread(void *arg) {
    worker_arg_t wa = *(worker_arg_t *)arg;
    free(arg);

    char username[128], password[128];
    snprintf(username, sizeof(username), "%s%d", DEFAULT_USERNAME_PREFIX, wa.user_index);
    snprintf(password, sizeof(password), "%s%d", DEFAULT_PASSWORD_PREFIX, wa.user_index);

    // 1) register (try, ignore failure if already exists)
    int reg_ok = do_register(wa.server_ip, wa.port, username, password);
    // it's OK if registration fails because user may exist; we still try login

    // 2) login & get cookie
    char cookie[COOKIE_MAX] = {0};
    int login_ok = do_login_and_get_cookie(wa.server_ip, wa.port, username, password, cookie, sizeof(cookie));
    if (!login_ok) {
        pthread_mutex_lock(&count_lock);
        total_fail++;
        pthread_mutex_unlock(&count_lock);
        printf("[User %d] LOGIN failed for %s (reg_ok=%d)\n", wa.user_index, username, reg_ok);
        return NULL;
    }

    // 3) perform uploads_per_user uploads
    int success_count = 0;
    for (int i = 0; i < wa.uploads_per_user; ++i) {
        int up = do_upload_with_cookie(wa.server_ip, wa.port, cookie, username, i+1);
        if (up) success_count++;
    }


    // 4) perform delete_file_per_user uploads
    // int del_ok = do_delete_with_cookie(wa.server_ip, wa.port ,cookie, username, 1);
    // if(!del_ok){
    //      pthread_mutex_lock(&count_lock);
    //     total_fail++;
    //     pthread_mutex_unlock(&count_lock);
    //     printf("[User %d] Delete failed for %s (del_ok=%d)\n", wa.user_index, username, del_ok);
    //     return NULL;
    // }
    // for(int i = 0; i < wa.user_index; ++i){

    // }

    pthread_mutex_lock(&count_lock);
    total_pass += success_count;
    total_fail += (wa.uploads_per_user - success_count);
    pthread_mutex_unlock(&count_lock);

    printf("[User %d] %s: uploaded %d/%d OK\n", wa.user_index, username, success_count, wa.uploads_per_user);
    return NULL;
}

// main ----------------------------------------------------------------------

int main(int argc, char **argv) {
    const char *ip = "127.0.0.1";
    int port = 8080;
    int num_users = 20;
    int uploads_per_user = 2;

    if (argc >= 2) ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);
    if (argc >= 4) num_users = atoi(argv[3]);
    if (argc >= 5) uploads_per_user = atoi(argv[4]);

    printf("Stress tester: server=%s:%d users=%d uploads/user=%d\n", ip, port, num_users, uploads_per_user);

    pthread_t *threads = malloc((size_t)num_users * sizeof(pthread_t));
    if (!threads) { perror("malloc"); return 1; }

    for (int i = 0; i < num_users; ++i) {
        worker_arg_t *wa = malloc(sizeof(worker_arg_t));
        strncpy(wa->server_ip, ip, sizeof(wa->server_ip)-1);
        wa->server_ip[sizeof(wa->server_ip)-1] = '\0';
        wa->port = port;
        wa->user_index = i + 1;
        wa->uploads_per_user = uploads_per_user;
        if (pthread_create(&threads[i], NULL, worker_thread, wa) != 0) {
            perror("pthread_create");
            free(wa);
        }
    }

    for (int i = 0; i < num_users; ++i) pthread_join(threads[i], NULL);

    printf("=== Summary ===\n");
    printf("Total uploads PASSED: %d\n", total_pass);
    printf("Total uploads FAILED: %d\n", total_fail);
    free(threads);

    return (total_fail == 0) ? 0 : 2;
}
