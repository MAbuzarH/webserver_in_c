#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080
#define NUM_CLIENTS 50      // how many concurrent connections
#define BUF_SIZE    8192

// Connect to server
int connect_to_server(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    return sock;
}

// Send GET request
int test_get_request() {
    int sock = connect_to_server(SERVER_IP, SERVER_PORT);
    if (sock < 0) return 0;

    const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sock, req, strlen(req), 0);

    char buf[BUF_SIZE];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);

    if (n <= 0) {
        printf("[GET] FAILED: no response\n");
        return 0;
    }
    buf[n] = '\0';
    if (strstr(buf, "200 OK")) {
        printf("[GET] PASSED\n");
        return 1;
    } else {
        printf("[GET] FAILED: %s\n", buf);
        return 0;
    }
}

// Send POST upload (small text)
int test_post_upload() {
    int sock = connect_to_server(SERVER_IP, SERVER_PORT);
    if (sock < 0) return 0;

    const char *boundary = "----TESTBOUNDARY";
    const char *body =
        "------TESTBOUNDARY\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "HelloStressTest!\r\n"
        "------TESTBOUNDARY--\r\n";

    char header[BUF_SIZE];
    snprintf(header, sizeof(header),
             "POST /upload HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %zu\r\n\r\n",
             boundary, strlen(body));

    send(sock, header, strlen(header), 0);
    send(sock, body, strlen(body), 0);

    char buf[BUF_SIZE];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);

    if (n <= 0) {
        printf("[POST] FAILED: no response\n");
        return 0;
    }
    buf[n] = '\0';
    if (strstr(buf, "200 OK")) {
        printf("[POST] PASSED\n");
        return 1;
    } else {
        printf("[POST] FAILED: %s\n", buf);
        return 0;
    }
}

int main() {
    int pass = 0, fail = 0;

    printf("=== Stress Test Started ===\n");

    // Run GET tests
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (test_get_request()) pass++; else fail++;
    }

    // Run POST upload tests
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (test_post_upload()) pass++; else fail++;
    }

    printf("=== Stress Test Finished ===\n");
    printf("PASSED: %d | FAILED: %d\n", pass, fail);

    return (fail == 0) ? 0 : 1;
}
