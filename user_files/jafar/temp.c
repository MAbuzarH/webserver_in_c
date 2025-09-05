#include<stdio.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<unistd.h>
#include<string.h>
#include<arpa/inet.h>

#define LISTENADDRESS "0.0.0.0"

struct sHttpreq{
char method[8];
char url[128];
};
typedef struct sHttpreq httpreq;

struct sFile{
    char filename[256]; // Corrected: A single character array for the filename.
    char *fc; //file content
    int size;
};
typedef struct sFile File;

char *error;

// return 0 on error else socketfd
int serv_init(int portno){
int sockfd;
struct sockaddr_in serv_addr;
sockfd = socket(AF_INET,SOCK_STREAM,0);
if(sockfd<0){
    error ="Socket() error \n";
 return 0;   
}

serv_addr.sin_family = AF_INET;
serv_addr.sin_port = htons(portno);
serv_addr.sin_addr.s_addr = inet_addr(LISTENADDRESS);

if(bind(sockfd, (const struct sockaddr *) &serv_addr, sizeof(serv_addr))){
     error ="Bind() error \n";
    close(sockfd);
    return 0;
}

if(listen(sockfd,5)){
    error ="listen() error \n";
    close(sockfd);
    return 0;
}

return sockfd;
}

/* return 0 on error else return client socket fd*/
int client_acpt(int s){
int c; //c = client fd;
struct sockaddr_in cli_addr;
socklen_t addrlength;
addrlength =0;
memset(&cli_addr,0,sizeof(cli_addr));
c = accept(s,(struct sockaddr *)&cli_addr,&addrlength);
if(c  < 0){
   error ="acepting() error \n";
   return 0;
}
return c;
}

// A helper function to find the next space and null-terminate a string.
// It returns a pointer to the character after the space.
char *find_and_null_terminate(char *start) {
    char *p = strchr(start, ' ');
    if (p != NULL) {
        *p = '\0'; // Null-terminate the string at the space.
        return p + 1; // Return a pointer to the character after the space.
    }
    return NULL; // Return NULL if no space is found.
}

httpreq *parse_http(char *str) {
    httpreq *req;
    char *p;
    char *next_token;

    // STEP 1: Allocate memory for the struct.
    req = malloc(sizeof(httpreq));
    if (req == NULL) {
        // Return NULL on memory allocation failure.
        error = "parse_http() error";
        return 0;
    }
    
    // Zeros out all bytes of the new struct. This is the correct way to do it.
    memset(req, 0, sizeof(httpreq));

    // STEP 2: Find the method. The method is the first word.
    next_token = find_and_null_terminate(str);
    if (next_token == NULL) {
        // Handle malformed request (no space found).
        free(req);
        return 0;
    }

    // Copy the method. It's safe now because we null-terminated the string.
    // Use strncpy to prevent buffer overflows.
    strncpy(req->method, str, sizeof(req->method) - 1);
    req->method[sizeof(req->method) - 1] = '\0'; // Ensure it's null-terminated.

    // STEP 3: Find the URL. The URL is the next word.
    // next_token now points to the character after the method.
    str = next_token;
    next_token = find_and_null_terminate(str);
    if (next_token == NULL) {
        // Handle malformed request (no space after URL, or no URL).
        free(req);
        return 0;
    }

    // Copy the URL.
    strncpy(req->url, str, sizeof(req->url) - 1);
    req->url[sizeof(req->url) - 1] = '\0'; // Ensure it's null-terminated.

    return req;
}

/*
 * Reads an entire HTTP request header from a socket.
 * It reads byte by byte until the \r\n\r\n sequence is found,
 * which marks the end of the HTTP headers.
 *
 * @param c The client socket file descriptor.
 * @return A pointer to the buffer containing the HTTP request, or NULL on error.
 */
char *cli_read(int c) {
    // The buffer is now a local variable, which is a safer practice.
    // It's allocated on the stack.
    static char buf[1024]; // Using a slightly larger buffer.
    char ch;
    int i = 0;
    
    memset(buf, 0, sizeof(buf));

    // Loop to read one character at a time until a full header is found.
    while (read(c, &ch, 1) > 0) {
        if (i < sizeof(buf) - 1) {
            buf[i++] = ch;
        }
        
        // Check for the end of the HTTP header (\r\n\r\n).
        if (i >= 4) {
            if (buf[i-4] == '\r' && buf[i-3] == '\n' &&
                buf[i-2] == '\r' && buf[i-1] == '\n') {
                
                buf[i] = '\0'; // Null-terminate the string.
                return buf; // Return the full request.
            }
        }
    }

    error= "read() error";
    // If we exit the loop, there was a read error or connection closed.
    return 0; 
}


/**
 * Sends the HTTP status line and all headers.
 * This is a single, combined function to ensure correct formatting.
 *
 * @param c The client socket file descriptor.
 * @param code The HTTP status code (e.g., 200, 404).
 * @param contentType The Content-Type header value (e.g., "text/html").
 * @param data The actual response body (HTML, JSON, etc.).
 */
void http_send_response(int c, int code, char *contentType, char *data) {
    char header_buf[1024];
    int n;

    snprintf(header_buf, sizeof(header_buf) - 1,
        "HTTP/1.0 %d OK\r\n"
        "Server: httpd.c\r\n"
        "Cache-Control: no-store, no-cache, max-age=0, private\r\n"
        "Content-Language: en\r\n"
        "Expires: -1\r\n"
        "X-Frame-Options: SAMEORIGIN\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n" // This is the crucial blank line that terminates the header section.
        , code, contentType, (int)strlen(data)
    );

    n = strlen(header_buf);
    write(c, header_buf, n);
    write(c, data, strlen(data)); // Write the body separately.
}

/**
 * Reads the entire contents of a file into a dynamically allocated struct.
 * This is a robust implementation that handles errors and dynamic resizing.
 *
 * @param filename The path to the file to read.
 * @return A pointer to a new File struct containing the file's content,
 * or NULL on any error.
 */
File *fileread(char *filename) {
    int n, fd;
    char buf[512]; // A buffer for reading chunks from the file.
    int total_read = 0;
    File *f;
    
    // STEP 1: Allocate memory for the main File struct.
    f = malloc(sizeof(File));
    if (f == NULL) {
        perror("malloc() error for File struct");
        return NULL;
    }

    // STEP 2: Open the file. Check for errors (open returns -1 on failure).
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open() error");
        free(f); // Clean up memory allocated for the struct.
        return NULL;
    }
    
    // STEP 3: Safely copy the filename. Use strncpy to prevent buffer overflows.
    strncpy(f->filename, filename, sizeof(f->filename) - 1);
    f->filename[sizeof(f->filename) - 1] = '\0'; // Ensure null-termination.

    // STEP 4: Allocate initial memory for file content.
    // Start with a buffer for at least one chunk.
    f->fc = malloc(512); 
    if (f->fc == NULL) {
        perror("malloc() error for file content");
        close(fd); // Close the file.
        free(f);   // Clean up the main struct.
        return NULL;
    }

    // STEP 5: Loop to read the file in chunks.
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        // STEP 5a: Check if we need more memory.
        // We reallocate before copying to ensure the pointer remains valid.
        void *realloc_ptr = realloc(f->fc, total_read + n);
        if (realloc_ptr == NULL) {
            perror("realloc() error");
            close(fd);
            free(f->fc);
            free(f);
            return NULL;
        }
        f->fc = realloc_ptr;

        // STEP 5b: Copy the data from the temporary buffer to our dynamic buffer.
        // Use memcpy for raw data.
        memcpy(f->fc + total_read, buf, n);
        
        // STEP 5c: Update the total bytes read.
        total_read += n;
    }

    // STEP 6: Handle potential read errors.
    if (n < 0) {
        perror("read() error");
        close(fd);
        free(f->fc);
        free(f);
        return NULL;
    }

    // STEP 7: All reading is complete. Finalize the struct.
    close(fd);
    f->size = total_read;
    
    // Shrink the buffer to the exact size to save memory.
    void *shrink_ptr = realloc(f->fc, f->size + 1);
    if (shrink_ptr == NULL) {
        // If shrinking fails, we still return the valid, slightly larger buffer.
        // It's a non-critical error, so no need to exit.
        f->fc[f->size] = '\0'; // Null-terminate for good measure.
    } else {
        f->fc = shrink_ptr;
        f->fc[f->size] = '\0'; // Null-terminate the new, smaller buffer.
    }
    
    return f;
}


/*listin client request and parse that request*/
void cli_conn(int s, int c){
httpreq *req;
char *p;
File *f;
char *res;

p = cli_read(c);

if(!p){
    fprintf(stderr,"%s \n",error);
    close(c);
    return;
}
req = parse_http(p);

if(!req){
    fprintf(stderr,"%s",error);
    close(c);
    return;
}
//  printf("Method: %s , URL: %s \n",req->method,req->url);
// Now, we'll make the server more dynamic by checking for any file in /img/ or /app/
    if (!(strcmp(req->method, "GET"))) {
        char temp_url[256];
        char *file_path;
        
        // Serve the main index page if the request is for the root ("/")
        if (strcmp(req->url, "/") == 0) {
            file_path = "index.html";
        } else {
            // Otherwise, get the file path from the URL
            snprintf(temp_url, sizeof(temp_url), ".%s", req->url);
            file_path = temp_url;
        }

        f = fileread(file_path);

        if (!f) {
            res = "file not found ";
            http_send_response(c, 404, "text/plain", res, strlen(res));
        } else {
            // Use the new get_content_type function to determine the MIME type
            content_type = get_content_type(file_path);
            http_send_response(c, 200, content_type, f->fc, f->size);
            free(f->fc);
            free(f);
        }
    } else {
        res = "Method not supported ";
        http_send_response(c, 405, "text/plain", res, strlen(res));
    }
 free(req);
 close(c);
 return;
}




int main(int argc,char *argv[]){

    int s,nsockfd,n;
    char* portno;

    if(argc<2){
        fprintf(stderr,"Listining <portno> %s ",argv[1]);
        return -1;
    }else{
       portno = argv[1];  
    }
    s = serv_init(atoi(portno));

    if(!s){
        fprintf(stderr,"Error: %s",error);
        return -1;
    }

    printf("listning... %s %s \n",LISTENADDRESS,portno);
    while (1)
    {
      nsockfd = client_acpt(s);

      if(!nsockfd){
        fprintf(stderr," %s/n",error);
        continue;
      }

      printf("incomming connections... \n");
     if(!fork()){
      cli_conn(s,nsockfd);
     }
    }
    
    return -1;
}

// #include<stdio.h>
// #include<stdlib.h>
// #include<netinet/in.h>
// #include<sys/types.h>
// #include<fcntl.h>
// #include<sys/socket.h>
// #include<unistd.h>
// #include<string.h>
// #include<arpa/inet.h>
// #include <errno.h>

// #define LISTENADDRESS "0.0.0.0"
// // #define LISTENADDRESS "127.0.0.1"


// struct sHttpreq{
// char method[8];
// char url[128];
// };
// typedef struct sHttpreq httpreq;

// struct sFile{
//     char  filename[64];
//     char *fc; //file content
//     int size;
// };
// typedef struct sFile File;

// char *error;
// // return 0 on error els socketfd
// int serv_init(int portno){
// int sockfd;
// struct sockaddr_in serv_addr;
// sockfd = socket(AF_INET,SOCK_STREAM,0);
// if(sockfd<0){
//     error ="Socket() error \n";
//  return 0;   
// }

// serv_addr.sin_family = AF_INET;
// serv_addr.sin_port = htons(portno);
// serv_addr.sin_addr.s_addr = inet_addr(LISTENADDRESS);

// if(bind(sockfd, (const struct sockaddr *) &serv_addr, sizeof(serv_addr))){
//      error ="Bind() error \n";
//     close(sockfd);
//     return 0;
// }

// if(listen(sockfd,5)){
//     error ="listen() error \n";
//     close(sockfd);
//     return 0;
// }

// return sockfd;
// }

// /* return 0 on error else return client socket fd*/
// int client_acpt(int s){
// int c; //c = client fd;
// struct sockaddr_in cli_addr;
// socklen_t addrlength;
// addrlength =0;
// memset(&cli_addr,0,sizeof(cli_addr));
// c = accept(s,(struct sockaddr *)&cli_addr,&addrlength);
// if(c  < 0){
//    error ="acepting() error \n";
//    return 0;
// }

// return c;
// }

// // A helper function to find the next space and null-terminate a string.
// // It returns a pointer to the character after the space.
// char *find_and_null_terminate(char *start) {
//     char *p = strchr(start, ' ');
//     if (p != NULL) {
//         *p = '\0'; // Null-terminate the string at the space.
//         return p + 1; // Return a pointer to the character after the space.
//     }
//     return NULL; // Return NULL if no space is found.
// }

// httpreq *parse_http(char *str) {
//     httpreq *req;
//     char *p;
//     char *next_token;

//     // STEP 1: Allocate memory for the struct.
//     req = malloc(sizeof(httpreq));
//     if (req == NULL) {
//         // Return NULL on memory allocation failure.
//         error = "parse_http() error";
//         return 0;
//     }
    
//     // Zeros out all bytes of the new struct. This is the correct way to do it.
//     memset(req, 0, sizeof(httpreq));

//     // STEP 2: Find the method. The method is the first word.
//     next_token = find_and_null_terminate(str);
//     if (next_token == NULL) {
//         // Handle malformed request (no space found).
//         free(req);
//         return 0;
//     }

//     // Copy the method. It's safe now because we null-terminated the string.
//     // Use strncpy to prevent buffer overflows.
//     strncpy(req->method, str, sizeof(req->method) - 1);
//     req->method[sizeof(req->method) - 1] = '\0'; // Ensure it's null-terminated.

//     // STEP 3: Find the URL. The URL is the next word.
//     // next_token now points to the character after the method.
//     str = next_token;
//     next_token = find_and_null_terminate(str);
//     if (next_token == NULL) {
//         // Handle malformed request (no space after URL, or no URL).
//         free(req);
//         return 0;
//     }

//     // Copy the URL.
//     strncpy(req->url, str, sizeof(req->url) - 1);
//     req->url[sizeof(req->url) - 1] = '\0'; // Ensure it's null-terminated.

//     return req;
// }



// /*
//  * Reads an entire HTTP request header from a socket.
//  * It reads byte by byte until the \r\n\r\n sequence is found,
//  * which marks the end of the HTTP headers.
//  *
//  * @param c The client socket file descriptor.
//  * @return A pointer to the buffer containing the HTTP request, or NULL on error.
//  */
// char *cli_read(int c) {
//     // The buffer is now a local variable, which is a safer practice.
//     // It's allocated on the stack.
//     static char buf[1024]; // Using a slightly larger buffer.
//     char ch;
//     int i = 0;
    
//     memset(buf, 0, sizeof(buf));

//     // Loop to read one character at a time until a full header is found.
//     while (read(c, &ch, 1) > 0) {
//         if (i < sizeof(buf) - 1) {
//             buf[i++] = ch;
//         }
        
//         // Check for the end of the HTTP header (\r\n\r\n).
//         if (i >= 4) {
//             if (buf[i-4] == '\r' && buf[i-3] == '\n' &&
//                 buf[i-2] == '\r' && buf[i-1] == '\n') {
                
//                 buf[i] = '\0'; // Null-terminate the string.
//                 return buf; // Return the full request.
//             }
//         }
//     }

//     error= "read() error";
//     // If we exit the loop, there was a read error or connection closed.
//     return 0; 
// }

// /*
// snprintf(buf, 511,
// "HTTP/1.0 %d OK\r\n"
// "Date: Fri, 05 May 2023 18:05:01 GMT\r\n"
// "Server: ATS\r\n"
// "Cache-Control: no-store, no-cache, max-age=0, private\r\n"
// "Content-Type: text/html\r\n"
// "Content-Language: en\r\n"
// "Expires: -1\r\n"
// "X-Frame-Options: SAMEORIGIN\r\n"
// "Content-Length: 12\r\n"
// "\r\n"
// );
// */

// /**
//  * Sends the HTTP status line and all headers.
//  * This is a single, combined function to ensure correct formatting.
//  *
//  * @param c The client socket file descriptor.
//  * @param code The HTTP status code (e.g., 200, 404).
//  * @param contentType The Content-Type header value (e.g., "text/html").
//  * @param data The actual response body (HTML, JSON, etc.).
//  */
// void http_send_response(int c, int code, char *contentType, char *data, int data_length) {
//     char header_buf[1024];
//     int n;

//     snprintf(header_buf, sizeof(header_buf) - 1,
//         "HTTP/1.0 %d OK\r\n"
//         "Server: httpd.c\r\n"
//         "Cache-Control: no-store, no-cache, max-age=0, private\r\n"
//         "Content-Language: en\r\n"
//         "Expires: -1\r\n"
//         "X-Frame-Options: SAMEORIGIN\r\n"
//         "Content-Type: %s\r\n"
//         "Content-Length: %d\r\n"
//         "\r\n" // This is the crucial blank line that terminates the header section.
//         , code, contentType, data_length
//     );

//     n = strlen(header_buf);
//     write(c, header_buf, n);
//     // Use the provided data_length for the binary write.
//     write(c, data, data_length); 
// }



// /**
//  * Determines the Content-Type based on a file extension.
//  * This is a crucial step for serving different types of content.
//  *
//  * @param path The file path or URL to check.
//  * @return A string containing the correct MIME type, or "text/plain" if unknown.
//  */
// char *get_content_type(const char *path){
//     char *extension = strrchr(path, '.');
//     if (extension == NULL) {
//         return "text/plain";
//     }

//     if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
//         return "text/html";
//     } else if (strcmp(extension, ".css") == 0) {
//         return "text/css";
//     } else if (strcmp(extension, ".js") == 0) {
//         return "application/javascript";
//     } else if (strcmp(extension, ".jpeg") == 0 || strcmp(extension, ".jpg") == 0) {
//         return "image/jpeg";
//     } else if (strcmp(extension, ".png") == 0) {
//         return "image/png";
//     } else if (strcmp(extension, ".gif") == 0) {
//         return "image/gif";
//     } else if (strcmp(extension, ".mp4") == 0) {
//         return "video/mp4";
//     }else {
//         return "text/plain";
//     }
// }

// /**
//  * Reads the entire HTTP request from the client socket.
//  * This is a more robust approach that reads until the connection closes or an error occurs.
//  *
//  * @param c The client socket file descriptor.
//  * @return A dynamically allocated string containing the full request (headers + body), or NULL on error.
//  */
// char *cli_read_all(int c) {
//     char *buffer = NULL;
//     size_t buffer_size = 1024;
//     size_t total_read = 0;
//     int bytes_read;

//     // Allocate an initial buffer
//     buffer = (char *)malloc(buffer_size);
//     if (buffer == NULL) {
//         perror("malloc() failed");
//         return NULL;
//     }

//     // Read from the socket until there's no more data
//     while (1) {
//         bytes_read = read(c, buffer + total_read, buffer_size - total_read - 1);
//         if (bytes_read == 0) {
//             // Connection closed
//             break;
//         }
//         if (bytes_read < 0) {
//             if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                 // No more data to read for now, so we can stop.
//                 break;
//             }
//             // A real error occurred
//             perror("read() failed");
//             free(buffer);
//             return NULL;
//         }
//         total_read += bytes_read;

//         // Check if we need to resize the buffer for more data
//         if (total_read >= buffer_size - 1) {
//             buffer_size *= 2; // Double the buffer size
//             char *new_buffer = (char *)realloc(buffer, buffer_size);
//             if (new_buffer == NULL) {
//                 perror("realloc() failed");
//                 free(buffer);
//                 return NULL;
//             }
//             buffer = new_buffer;
//         }
//     }
//     buffer[total_read] = '\0'; // Null-terminate the string

//     if (total_read == 0) {
//         free(buffer);
//         return NULL; // No data read
//     }

//     return buffer;
// }
// /**
//  * Reads the entire contents of a file into a dynamically allocated struct.
//  * This is a robust implementation that handles errors and dynamic resizing.
//  *
//  * @param filename The path to the file to read.
//  * @return A pointer to a new File struct containing the file's content,
//  * or NULL on any error.
//  */
// File *fileread(char *filename) {
//     int n, fd;
//     char buf[512]; // A buffer for reading chunks from the file.
//     int total_read = 0;
//     File *f;
    
//     // STEP 1: Allocate memory for the main File struct.
//     f = malloc(sizeof(File));
//     if (f == NULL) {
//         perror("malloc() error for File struct");
//         return NULL;
//     }

//     // STEP 2: Open the file. Check for errors (open returns -1 on failure).
//     fd = open(filename, O_RDONLY);
//     if (fd < 0) {
//         perror("open() error");
//         free(f); // Clean up memory allocated for the struct.
//         return NULL;
//     }
    
//     // STEP 3: Safely copy the filename. Use strncpy to prevent buffer overflows.
//     strncpy(f->filename, filename, sizeof(f->filename) - 1);
//     f->filename[sizeof(f->filename) - 1] = '\0'; // Ensure null-termination.

//     // STEP 4: Allocate initial memory for file content.
//     // Start with a buffer for at least one chunk.
//     f->fc = malloc(512); 
//     if (f->fc == NULL) {
//         perror("malloc() error for file content");
//         close(fd); // Close the file.
//         free(f);   // Clean up the main struct.
//         return NULL;
//     }

//     // STEP 5: Loop to read the file in chunks.
//     while ((n = read(fd, buf, sizeof(buf))) > 0) {
//         // STEP 5a: Check if we need more memory.
//         // We reallocate before copying to ensure the pointer remains valid.
//         void *realloc_ptr = realloc(f->fc, total_read + n);
//         if (realloc_ptr == NULL) {
//             perror("realloc() error");
//             close(fd);
//             free(f->fc);
//             free(f);
//             return NULL;
//         }
//         f->fc = realloc_ptr;

//         // STEP 5b: Copy the data from the temporary buffer to our dynamic buffer.
//         // Use memcpy for raw data.
//         memcpy(f->fc + total_read, buf, n);
        
//         // STEP 5c: Update the total bytes read.
//         total_read += n;
//     }

//     // STEP 6: Handle potential read errors.
//     if (n < 0) {
//         perror("read() error");
//         close(fd);
//         free(f->fc);
//         free(f);
//         return NULL;
//     }

//     // STEP 7: All reading is complete. Finalize the struct.
//     close(fd);
//     f->size = total_read;
    
//     // Shrink the buffer to the exact size to save memory.
//     void *shrink_ptr = realloc(f->fc, f->size + 1);
//     if (shrink_ptr == NULL) {
//         // If shrinking fails, we still return the valid, slightly larger buffer.
//         // It's a non-critical error, so no need to exit.
//         f->fc[f->size] = '\0'; // Null-terminate for good measure.
//     } else {
//         f->fc = shrink_ptr;
//         f->fc[f->size] = '\0'; // Null-terminate the new, smaller buffer.
//     }
    
//     return f;
// }


// /*listin client request and parse that request*/
// void cli_conn(int s, int c){
// httpreq *req;
// char *p;
// char str[96];
// File *f;

// p = cli_read(c);

// if(!p){
//     fprintf(stderr,"%s \n",error);
//     close(c);
//     return;
// }
// req = parse_http(p);

// if(!req){
//     fprintf(stderr,"%s",error);
//     close(c);
//     return;
// }
// //  printf("Method: %s , URL: %s \n",req->method,req->url);
// char *res ;


// if(!(strcmp(req->method,"GET"))){
//     char temp_url[256];
//     memset(temp_url,0,256);
//     char *file_path;
//     char *conten_type;
//     if((strcmp(req->url,"/") == 0)){
//         file_path = "index.html"; 
//     }
//     else{
//     snprintf(temp_url, sizeof(temp_url),".%s",req->url);
//     file_path = temp_url;
//     }
   
//     f = fileread(file_path);

//     if(!f){
//      res = "File not found";
//      http_send_response(c,404,"text/plain",res,strlen(res));
//     }else{
//        conten_type = get_content_type(file_path);
//        http_send_response(c,200,conten_type,f->fc,f->size);
//        free(f->fc); // Free file content after sending
//        free(f); // Free the struct  
//     }
// }else if(!(strcmp(req->method,"POST"))){
//   //Call the new POST handler
//         // handle_post_request(c, req, p);
//         // p is full_request_data
//          char *header_end = strstr(p, "\r\n\r\n");
//         char *body_data = NULL;
        
//         if (header_end) {
//             body_data = header_end + 4; // Move pointer past the \r\n\r\n
//             printf("Received POST data: %s\n", body_data);

//             // Save the data to a file (append mode)
//             FILE *fp = fopen("form_data.txt", "w");
//             if (fp) {
//                 fprintf(fp, "Received POST data: %s\n", body_data);
//                 fclose(fp);
//             }else{
//                 perror("file not open");
//             }
//         }
        
//         // Send a success response back to the client
//         res = "<h2>Data Submitted Successfully!</h2><p>Check the form_data.txt file on the server.</p>";
//         http_send_response(c, 200, "text/html", res, strlen(res));
//     }else{
//     res = "Method not supported ";
//     http_send_response(c, 405, "text/plain", res, strlen(res));
// }


//  free(req);
//  close(c);
//  return;
// }




// int main(int argc,char *argv[]){

//     int s,nsockfd,n;
//     char* portno;

//     if(argc<2){
//         fprintf(stderr,"Listining <portno> %s ",argv[1]);
//         return -1;
//     }else{
//        portno = argv[1];  
//     }
//     s = serv_init(atoi(portno));

//     if(!s){
//         fprintf(stderr,"Error: %s",error);
//         return -1;
//     }

//     printf("listning... %s %s \n",LISTENADDRESS,portno);
//     while (1)
//     {
//         /* code */
//       nsockfd = client_acpt(s);

//       if(!nsockfd){
//         fprintf(stderr," %s/n",error);
//         continue;
//       }

//       printf("incomming connections... \n");
//       /*fork() returns 0 for new process and 
//       new process id for parent process
//       here in my code s = server fd and nsockfd = client fd; 
//       */
//      if(!fork()){
//       cli_conn(s,nsockfd);
//      }
//     }
    

//     return -1;
// }


// //code tried

// /**
//  * Reads the HTTP request body based on the Content-Length header.
//  *
//  * @param c The client socket file descriptor.
//  * @param full_request_header The full HTTP request header string.
//  * @return A pointer to the dynamically allocated body data, or NULL on error.
//  */
// // char *read_http_body(int c, char *full_request_header) {
// //     int content_length = 0;
// //     char *body_data = NULL;
// //     char *header_copy = NULL;

// //     // Use strdup to make a copy of the header string,
// //     // as strtok modifies the string in-place.
// //     header_copy = strdup(full_request_header);
// //     if (!header_copy) {
// //         return NULL;
// //     }

// //     // Use strtok to split the headers line by line, separated by \r\n
// //     char *line = strtok(header_copy, "\r\n");
// //     while (line != NULL) {
// //         // Check if the current line starts with "Content-Length:"
// //         if (strstr(line, "Content-Length:") == line) {
// //             // We found the Content-Length header.
// //             // Move the pointer past the header name and any whitespace.
// //             char *value_ptr = line + strlen("Content-Length:");
// //             while (*value_ptr && (*value_ptr == ' ' || *value_ptr == '\t')) {
// //                 value_ptr++;
// //             }
// //             // Use atoi to convert the remaining string to an integer.
// //             content_length = atoi(value_ptr);
// //             break;
// //         }
// //         // Get the next line
// //         line = strtok(NULL, "\r\n");
// //     }
    
// //     // Free the copied string
// //     free(header_copy);

// //     // If we didn't find the Content-Length or it was 0, return NULL.
// //     if (content_length <= 0) {
// //         return NULL;
// //     }

// //     // Allocate memory for the body data
// //     body_data = (char *)malloc(content_length + 1);
// //     if (!body_data) {
// //         return NULL;
// //     }

// //     // Read the body data from the socket
// //     int total_read = 0;
// //     int bytes_read;
// //     while (total_read < content_length) {
// //         bytes_read = read(c, body_data + total_read, content_length - total_read);
// //         if (bytes_read <= 0) {
// //             free(body_data);
// //             return NULL;
// //         }
// //         total_read += bytes_read;
// //     }

// //     // Null-terminate the string for safety and for future string functions
// //     body_data[total_read] = '\0';
// //     return body_data;
// // }

// // New function to handle POST requests
// // void handle_post_request(int c, httpreq *req, char *full_request) {
// //     char *body_data;
// //     char *res;

// //     // Read the request body
// //     body_data = cli_read_all(c);
    
// //     // Check if the body data was successfully read
// //     if (!body_data) {
// //         res = "Error: Could not read request body.";
// //         http_send_response(c, 400, "text/plain", res, strlen(res));
// //         return;
// //     }

// //     // You would parse the body_data here. For now, we'll just print it.
// //     printf("Received POST data: %s\n", body_data);

// //     // Save the data to a file (append mode)
// //     FILE *fp = fopen("form_data.txt", "a");
// //     if (fp) {
// //         fprintf(fp, "Received POST data: %s\n", body_data);
// //         fclose(fp);
// //     }

// //     // Send a success response back to the client
// //     res = "<h2>Data Submitted Successfully!</h2><p>Check the form_data.txt file on the server.</p>";
// //     http_send_response(c, 200, "text/html", res, strlen(res));
    
// //     // Free the dynamically allocated memory
// //     free(body_data);
// // }


// // {
// //     char *template;
// //     httpreq *req;
// //     char buf[512];
// //     template =
// // "GET /sdfsdfd HTTP/1.1\r\n"
// // "Host: fagelsjo.net:8184\r\n"
// // "Upgrade-Insecure-Requests: 1\r\n"
// //"Accept: text/html,application/xhtml+xml,application/xml;q=0.9, */*;q=0.8\r\n"
// // "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)\r\n"
// // "Version/16.3 Safari/605.1.15\r\n"
// // "Accept-language: en-GB,en;q=0.9\r\n"
// // "Accept-Encoding: gzip, deflate\r\n"
// // "Connection: keep-alive\r\n"
// // "\r\n";
// //     n= strlen(template);
// //     memset(buf,0,512);
// //     strcpy(buf,template);
// //     req = parse_http(buf);
   
// //     printf("Method: %s , URL: %s \n",req->method,req->url);
// //     free(req);
// //     return 0;

// // File *fileread (char *filename){
// //     int n,x,fd;
// //     char buf[512];
// //     char *p;
// //     File *f;
// //     f = malloc(sizeof(struct sFile));

// //     fd = open(filename,O_RDONLY);
// //     if(!fd){
// //         close(fd);
// //         return 0;
// //     }

// //     strcpy(f->filename,filename);
// //     f->fc=malloc(512);

// //     x = 0;
// //     while(1){

// //         memset(buf,0,512);
// //         n = read(fd,buf,512);

// //         if(!n){
// //            break; 
// //         }else if(x == -1){
// //           close(fd);
// //           free(f->fc);
// //           free(f);
// //          return 0;
// //         }
        
// //         strncpy(buf,(f->fc )+x,n);
// //         x += n;
// //         f->fc = realloc(f->fc,(512+x));
        
// //     }
// //     f->size=x;
// //     close(fd);
// //     return f;
// // }


// // if(!(strcmp(req->method ,"GET"))  && !(strncmp(req->url ,"/img/",5)) ){
// //     memset(temp_url,0,150);
// //     snprintf(temp_url, sizeof(temp_url),".%s",req->url);
// //     f = fileread(temp_url);

// //     if(!f){
// //          res= "file not found ";
// //          http_send_response(c,404,"text/plain",res,strlen(res));
// //     }else{
// //         http_send_response(c,200,"image/jpeg",f->fc,f->size);
// //         free(f->fc); // Free file content after sending
// //         free(f); // Free the struct
// //     }  
// // }



// // else if(!(strcmp(req->method ,"GET"))  && !(strcmp(req->url ,"/app/webpage")) ){
// //    // res="<html> <p > helow world </p><p > good to see you </p> </html>";
// //      res="<html> <body> <img src='/img/test.jpg' width='200px' height='200px' alt='image' /> </body>  </html>";
// //      http_send_response(c,200,"text/html",res,strlen(res));
// // // http_headers(c,200);
// // // http_responce(c,"text/html",res);

// // }else{

// //     res= "file not found ";
// //     http_send_response(c,404,"text/plain",res,strlen(res));
// //     // http_headers(c,404);
// //     // http_responce(c,"text/plain",res);   
// // }

// /*return 0 on error else struct httpreq*/
// // httpreq *parse_http(char *str){
// // httpreq *req;
// // char *p;

// // printf("x/n ");
// // req = malloc(sizeof(httpreq));
// // //memset(&req,0,sizeof(httpreq)); 
// // // if(req != NULL){
// // //  memset(&req,0,sizeof(httpreq));   
// // // }


// // for(p=str; p && *p == ' '; p++);
// //     if(*p == ' '){
// //      *p=0;
// //     }else{
// //         error="parse_http(): tamplate parse error";
// //         free(req);
// //         return 0;
// //     }

// // strcpy(req->method,str);

// // return req;
// // }

// /*return 0 on error else data*/
// // char *cli_read(int c){
// // static char buf[512];
// // memset(buf,0,511);

// // if(read(c,buf,511) < 0){
// //     error="read() error";
// //     // close(s);
// //     return 0;
// // }
// // return buf;
// // }


// // void http_responce(int c,char *contentType,char *data){
// // char header_buf[512];
// // int n;
// // n=strlen(data);
// // memset(header_buf,0,512);
// // snprintf(header_buf, 511,
// // "Content-Type: %s\r\n"
// // "Content-Length: %d\r\n"
// // "%s \r\n"
// // "\r\n"

// // ,contentType,n,data
// // );
// // n = strlen(header_buf);
// // write(c,header_buf,n);
// // //write actual data 
// // //write(c, data, n);
// // return;
// // }

// // 1 when everything is fine else 0
// // int send_file(int c, char *contenttype,File *file){
// // if(!file){
// //     return 0;
// // }
// //     // http_responce(c,contenttype,file->fc);
// //     char header_buf[512];
// //     int n ,x;
// //     char *p;

// //     snprintf(header_buf, 511,
// //         "Content-Type: %s\r\n"
// //         "Content-Length: %d\r\n"
// //         "\r\n" // This blank line separates these headers from the body.
// //         , contenttype, file->size
// //     );

// //   n = file->size;
// //   p = file->fc; 
// //  while (1)
// //  {
// //     // n = strlen(header_buf);
// //     x= write(c, p, (n<512)? n: 512);
// //     if(x < 0)
// //     {
// //         return 0;
// //     }
// //      n-= x;
// //      if(n <0)
// //     break;
// //     else 
// //      p += n;
     
// //  }
 
// //     return 1;
// // }

// // char temp_url[150];
// // if(!(strcmp(req->method ,"GET"))  && !(strncmp(req->url ,"/img/",5)) ){
// //     memset(temp_url,0,150);
// //     snprintf(temp_url, sizeof(temp_url),".%s",req->url);
// //     f = fileread(temp_url);

// //     if(!f){
// //           res= "file not found ";
// //          http_headers(c,404); //page not found
// //         http_responce(c,"text/plain",res);
// //         perror("error in !f");
// //     }else{

       
// //    if(!send_file(c,"img/jpg",f)){
// //     res= "file not found ";
// //      perror("error file not fide ");
// //          //http_headers(c,500); //page not found
// //         http_responce(c,"text/plain",res);
// //    }

// //    res="<html> <img src=img/test.jpg alt='image' /> </html>";
// //     http_headers(c,200);
// //     http_responce(c,"image/jpg",res);
// //     }  
// // }


// // void http_send_response(int c, int code, char *contentType, char *data) {
// //     char header_buf[1024];
// //     int n;

// //     snprintf(header_buf, sizeof(header_buf) - 1,
// //         "HTTP/1.0 %d OK\r\n"
// //         "Server: httpd.c\r\n"
// //         "Cache-Control: no-store, no-cache, max-age=0, private\r\n"
// //         "Content-Language: en\r\n"
// //         "Expires: -1\r\n"
// //         "X-Frame-Options: SAMEORIGIN\r\n"
// //         "Content-Type: %s\r\n"
// //         "Content-Length: %d\r\n"
// //         "\r\n" // This is the crucial blank line that terminates the header section.
// //         , code, contentType, (int)strlen(data)
// //     );

// //     n = strlen(header_buf);
// //     write(c, header_buf, n);
// //     write(c, data, strlen(data)); // Write the body separately.
// // }


// // void http_headers(int c,int code){
// // char buf[512];
// // int n;
// // memset(buf,0,512);
// // snprintf(buf, 511,
// // "HTTP/1.0 %d OK\r\n"
// // "Server: httpd.c\r\n"
// // "Cache-Control: no-store, no-cache, max-age=0, private\r\n"
// // "Content-Language: en\r\n"
// // "Expires: -1\r\n"
// // "X-Frame-Options: SAMEORIGIN\r\n"

// // ,code
// // );

// // n = strlen(buf);
// // write(c,buf,n);
// // return;
// // }

// // void http_responce(int c, char *contentType, char *data) {
   
    
// //     // First, send the Content-Type and Content-Length headers.
// //     char header_buf[512];
// //     int n =  strlen(data);

// //     snprintf(header_buf, 511,
// //         "Content-Type: %s\r\n"
// //         "Content-Length: %d\r\n"
// //         "\r\n" // This blank line separates these headers from the body.
// //         , contentType, n
// //     );

// //     write(c, header_buf, strlen(header_buf));

// //     // Then, write the actual data (the body).
// //     write(c, data, n);

// //     return;
// // }


// char *read_full_request(int c) {
//     char *buffer = malloc(MAX_REQUEST_SIZE);
//     if (!buffer) {
//         perror("malloc() failed");
//         return NULL;
//     }
    
//     // Set a timeout to prevent the server from hanging on a slow connection.
//     struct timeval tv;
//     tv.tv_sec = 30; // 5 second timeout
//     tv.tv_usec = 0;
//     setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

//     int bytes_read = 0;
//     int total_read = 0;
//     while (total_read < MAX_REQUEST_SIZE - 1) {
//         bytes_read = read(c, buffer + total_read, 1);
//         if (bytes_read <= 0) {
//             // End of file or error
//             if (bytes_read < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
//                 // Timeout occurred, we have what we have.
//             } else {
//                 perror("read() failed");
//                 free(buffer);
//                 return NULL;
//             }
//             break;
//         }
//         total_read += bytes_read;
        
//         // Check if we've read the full header (\r\n\r\n).
//         if (total_read >= 4 && strstr(buffer, "\r\n\r\n")) {
//             // If it's a POST request, we need to continue reading the body
//             if (strncmp(buffer, "POST", 4) == 0) {
//                 // Find Content-Length header.
//                 char *content_length_str = strstr(buffer, "Content-Length: ");
//                 if (content_length_str) {
//                     int content_length = atoi(content_length_str + strlen("Content-Length: "));
//                     // Read the rest of the body
//                     int body_read = 0;
//                     char *body_start = strstr(buffer, "\r\n\r\n") + 4;
//                     // Read the remaining bytes.
//                     int bytes_to_read = content_length - (total_read - (body_start - buffer));
//                     if (bytes_to_read > 0) {
//                         read(c, buffer + total_read, bytes_to_read);
//                         total_read += bytes_to_read;
//                     }
//                 }
//             }
//             break; // Stop reading after the header and body.
//         }
//     }
//     buffer[total_read] = '\0';
//     return buffer;
// }




// //comments end

// // }


