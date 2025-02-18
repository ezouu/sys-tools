#include <stdio.h>      
#include <stdlib.h>
#include <unistd.h>     
#include <string.h>
#include <ctype.h>     
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "listener_socket.h"
#include "iowrapper.h"
#include "protocol.h"
#define ERR_PORT "Invalid Port\n"
#define MAX_HEADER_SIZE 2048

static const char *BODY_200 = "OK\n";
static const char *BODY_201 = "Created\n";
static const char *BODY_400 = "Bad Request\n";
static const char *BODY_403 = "Forbidden\n";
static const char *BODY_404 = "Not Found\n";
static const char *BODY_500 = "Internal Server Error\n";
static const char *BODY_501 = "Not Implemented\n";
static const char *BODY_505 = "Version Not Supported\n";

typedef enum {
    S_OK = 200,
    S_CREATED = 201,
    S_BAD_REQUEST = 400,
    S_FORBIDDEN = 403,
    S_NOT_FOUND = 404,
    S_INTERNAL_ERR = 500,
    S_NOT_IMPLEMENTED = 501,
    S_VERSION_NOT_SUPP = 505
} status_code_t;

typedef struct {
    char method[9];      
    char uri[65];        
    char version[10];   
    size_t content_length;
    int have_content_length;
    int valid;
} http_request_t;

static int writen(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *ptr = buf;
    while (total < len) {
        ssize_t n = write(fd, ptr + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue; 
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}
static void drain_socket(int fd) {
    char tmp[1024];
    while (1) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }
}
static const char *status_phrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "Version Not Supported";
        default:  return "Unknown";
    }
}

static const char *status_body(int code) {
    switch (code) {
        case 200: return BODY_200;
        case 201: return BODY_201;
        case 400: return BODY_400;
        case 403: return BODY_403;
        case 404: return BODY_404;
        case 500: return BODY_500;
        case 501: return BODY_501;
        case 505: return BODY_505;
        default:  return "Internal Server Error\n";
    }
}
static void send_response(int fd, int code, const char *body, size_t body_len) {
    if (body == NULL) {
        body = status_body(code);
        body_len = strlen(body);
    }

    char header_buf[512];
    const char *phrase = status_phrase(code);
    int n = snprintf(header_buf, sizeof(header_buf),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Length: %zu\r\n"
                     "\r\n",
                     code, phrase, body_len);
    if (writen(fd, header_buf, (size_t)n) < 0) {
        return; 
    }
    if (body_len > 0) {
        writen(fd, body, body_len);
    }
}

static int validate_http_version(const char *v) {
    if (strncmp(v, "HTTP/", 5) != 0) {
        return 0;  // not even "HTTP/"
    }
    if (strlen(v) != 8) {
        return 0; 
    }
    if (!isdigit((unsigned char)v[5]) || v[6] != '.' || !isdigit((unsigned char)v[7])) {
        return 0; 
    }
    if (strcmp(v, "HTTP/1.1") == 0) {
        return 1;
    }
    return 2;
}
static int validate_request_line(http_request_t *req) {
    // check method
    size_t mlen = strlen(req->method);
    if (mlen < 1 || mlen > 8) {
        return 0;
    }
    for (size_t i = 0; i < mlen; i++) {
        if (!isalpha((unsigned char)req->method[i])) {
            return 0;
        }
    }

    // check URI
    size_t ulen = strlen(req->uri);
    if (ulen < 2 || ulen > 64) {
        return 0;
    }
    if (req->uri[0] != '/') {
        return 0;
    }
    for (size_t i = 1; i < ulen; i++) {
        char c = req->uri[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' ||
              c == '-')) {
            return 0;
        }
    }

    // version
    int ver = validate_http_version(req->version);
    if (ver == 1) {
        return 1;   // perfect => HTTP/1.1
    } else if (ver == 2) {
        return -1;  // well-formed but unsupported => want 505
    } else {
        return 0;   // not "HTTP/" => 400
    }
}
static int parse_headers_and_request_line(const char *buf, http_request_t *req) {
    req->method[0]  = '\0';
    req->uri[0]     = '\0';
    req->version[0] = '\0';
    req->content_length   = 0;
    req->have_content_length = 0;
    req->valid = 0;
    const char *line_end = strstr(buf, "\r\n");
    if (!line_end) {
        return 400; 
    }
    size_t line_len = (size_t)(line_end - buf);
    char req_line[256];
    if (line_len >= sizeof(req_line)) {
        return 400;
    }
    memcpy(req_line, buf, line_len);
    req_line[line_len] = '\0';
    int tokens = sscanf(req_line, "%8s %64s %9s",
                        req->method, req->uri, req->version);
    if (tokens != 3) {

        return 400;
    }
    int val = validate_request_line(req);
    if (val == 1) {
        if (strcasecmp(req->method, "GET") != 0 &&
            strcasecmp(req->method, "PUT") != 0) {
            return 501;
        }
        req->valid = 1;
    } else if (val == -1) {
        return 505;
    } else {
        return 400;
    }
    const char *headers_start = line_end + 2;  
    const char *blank = strstr(headers_start, "\r\n\r\n");
    if (!blank) {
        return 400;
    }
    const char *cur = headers_start;
    while (cur < blank) {
        // Find the next "\r\n"
        const char *hdr_end = strstr(cur, "\r\n");
        if (!hdr_end || hdr_end > blank) {
            // No more valid header lines
            break;
        }
        size_t hdr_len = (size_t)(hdr_end - cur);
        if (hdr_len == 0) {
            // empty line => done
            break;
        }
        char hdr_line[256];
        if (hdr_len >= sizeof(hdr_line)) {
            // too big for our local buffer
            return 400;
        }
        memcpy(hdr_line, cur, hdr_len);
        hdr_line[hdr_len] = '\0';
        char *colon = strchr(hdr_line, ':');
        if (!colon) {
            return 400;
        }
        *colon = '\0'; 
        char *key = hdr_line;
        char *value = colon + 1;
        while (*value == ' ' || *value == '\t') {
            value++;
        }
        for (char *p = key; *p; p++) {
            if (! ( (*p >= 'a' && *p <= 'z') ||
                    (*p >= 'A' && *p <= 'Z') ||
                    (*p >= '0' && *p <= '9') ||
                    (*p == '.') ||
                    (*p == '-') ) ) {
                return 400;
            }
        }
        size_t klen = strlen(key);
        if (klen < 1 || klen > 128) {
            return 400;
        }
        size_t vlen = strlen(value);
        if (vlen < 1 || vlen > 128) {
            return 400;
        }
        for (size_t i = 0; i < vlen; i++) {
            if (value[i] < 32 || value[i] > 126) {
                return 400; 
            }
        }
        if (strcasecmp(key, "Content-Length") == 0) {
            for (size_t i = 0; i < strlen(value); i++) {
                if (!isdigit((unsigned char)value[i])) {
                    return 400;
                }
            }
            unsigned long long cl = strtoull(value, NULL, 10);
            if (cl > 0x7fffffffULL) {
                return 400;
            }
            req->content_length = (size_t)cl;
            req->have_content_length = 1;
        }
        cur = hdr_end + 2;
    }

    if (strcasecmp(req->method, "PUT") == 0) {
        if (!req->have_content_length) {
            return 400;
        }
    }

    req->valid = 1;
    return 0;
}

static int handle_get(int fd, const char *filepath) {
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        if (errno == ENOENT) {
            send_response(fd, S_NOT_FOUND, NULL, 0);
            return S_NOT_FOUND;
        } else if (errno == EACCES) {
            send_response(fd, S_FORBIDDEN, NULL, 0);
            return S_FORBIDDEN;
        } else {
            send_response(fd, S_INTERNAL_ERR, NULL, 0);
            return S_INTERNAL_ERR;
        }
    }
    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        close(file_fd);
        send_response(fd, S_INTERNAL_ERR, NULL, 0);
        return S_INTERNAL_ERR;
    }
    if (!S_ISREG(st.st_mode)) {
        close(file_fd);
        send_response(fd, S_FORBIDDEN, NULL, 0);
        return S_FORBIDDEN;
    }
    size_t fsize = (size_t)st.st_size; 
    {
        char header_buf[512];
        const char *phrase = status_phrase(S_OK);
        int n = snprintf(header_buf, sizeof(header_buf),
                         "HTTP/1.1 %d %s\r\n"
                         "Content-Length: %zu\r\n"
                         "\r\n",
                         S_OK, phrase, fsize);
        if (writen(fd, header_buf, (size_t)n) < 0) {
            close(file_fd);
            return S_INTERNAL_ERR; 
        }
    }
    {
        char buffer[4096];
        size_t bytes_left = fsize;
        while (bytes_left > 0) {
            size_t chunk = (bytes_left < sizeof(buffer)) ? bytes_left : sizeof(buffer);
            ssize_t r = read(file_fd, buffer, chunk);
            if (r < 0) {
                close(file_fd);
                return S_INTERNAL_ERR;
            }
            if (r == 0) {
                break;
            }
            if (writen(fd, buffer, (size_t)r) < 0) {
                close(file_fd);
                return S_INTERNAL_ERR;
            }
            bytes_left -= (size_t)r;
        }
    }

    close(file_fd);
    return S_OK;
}

static void handle_connection(int client_fd) {
    char header_buf[MAX_HEADER_SIZE + 1];
    memset(header_buf, 0, sizeof(header_buf));

    size_t total_read = 0;
    int found_double_crlf = 0;
    while (total_read < MAX_HEADER_SIZE) {
        ssize_t n = read(client_fd, header_buf + total_read, MAX_HEADER_SIZE - total_read);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            send_response(client_fd, S_BAD_REQUEST, NULL, 0);
            drain_socket(client_fd);
            return;
        }
        if (n == 0) {
            send_response(client_fd, S_BAD_REQUEST, NULL, 0);
            drain_socket(client_fd);
            return;
        }
        total_read += (size_t)n;
        if (strstr(header_buf, "\r\n\r\n") != NULL) {
            found_double_crlf = 1;
            break;
        }
    }
    if (!found_double_crlf) {

        send_response(client_fd, S_BAD_REQUEST, NULL, 0);
        drain_socket(client_fd);
        return;
    }
    http_request_t req;
    int parse_code = parse_headers_and_request_line(header_buf, &req);
    if (parse_code != 0) {

        send_response(client_fd, parse_code, NULL, 0);
        drain_socket(client_fd);
        return;
    }
    if (!req.valid) {

        send_response(client_fd, S_BAD_REQUEST, NULL, 0);
        drain_socket(client_fd);
        return;
    }
    if (strcasecmp(req.method, "GET") != 0 && strcasecmp(req.method, "PUT") != 0) {
        send_response(client_fd, S_NOT_IMPLEMENTED, NULL, 0);
        drain_socket(client_fd);
        return;
    }
    if (strcmp(req.version, "HTTP/1.1") != 0) {
        // 505
        send_response(client_fd, S_VERSION_NOT_SUPP, NULL, 0);
        drain_socket(client_fd);
        return;
    }
    const char *uri_path = req.uri + 1; 

    if (strcasecmp(req.method, "GET") == 0) {
        handle_get(client_fd, uri_path);
        drain_socket(client_fd);
        return;
    } else {
        char *body_start = strstr(header_buf, "\r\n\r\n");
        body_start += 4; // skip that boundary
        size_t header_part_len = (size_t)((header_buf + total_read) - body_start);

        size_t need_to_read = 0;
        if (req.content_length > header_part_len) {
            need_to_read = req.content_length - header_part_len;
        }
        int created = 0;
        struct stat st;
        int stat_ret = stat(uri_path, &st);
        if (stat_ret < 0 && errno == ENOENT) {
            created = 1; // new file
        }

        int file_fd = open(uri_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd < 0) {
            if (errno == EACCES) {
                send_response(client_fd, S_FORBIDDEN, NULL, 0);
            } else {
                send_response(client_fd, S_INTERNAL_ERR, NULL, 0);
            }

            size_t drain_amt = need_to_read;

            char drain_buf[1024];
            while (drain_amt > 0) {
                size_t chunk = (drain_amt > sizeof(drain_buf)) ? sizeof(drain_buf) : drain_amt;
                ssize_t r = read(client_fd, drain_buf, chunk);
                if (r <= 0) {
                    break;
                }
                drain_amt -= (size_t)r;
            }
            drain_socket(client_fd);
            return;
        }
        size_t left_off = 0;
        while (left_off < header_part_len) {
            ssize_t w = write(file_fd, body_start + left_off, header_part_len - left_off);
            if (w < 0) {
                if (errno == EINTR) continue;
                close(file_fd);
                send_response(client_fd, S_INTERNAL_ERR, NULL, 0);

                // Drain the rest
                size_t drain_amt = need_to_read;
                char drain_buf[1024];
                while (drain_amt > 0) {
                    size_t chunk = (drain_amt > sizeof(drain_buf)) ? sizeof(drain_buf) : drain_amt;
                    ssize_t r = read(client_fd, drain_buf, chunk);
                    if (r <= 0) {
                        break;
                    }
                    drain_amt -= (size_t)r;
                }
                drain_socket(client_fd);
                return;
            }
            left_off += (size_t)w;
        }
        size_t bytes_to_go = need_to_read;
        #define PUT_CHUNK 4096
        char buffer[PUT_CHUNK];
        while (bytes_to_go > 0) {
            size_t chunk = (bytes_to_go > PUT_CHUNK) ? PUT_CHUNK : bytes_to_go;
            ssize_t r = read(client_fd, buffer, chunk);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(file_fd);
                send_response(client_fd, S_INTERNAL_ERR, NULL, 0);
                drain_socket(client_fd);
                return;
            }
            if (r == 0) {
                close(file_fd);
                send_response(client_fd, S_INTERNAL_ERR, NULL, 0);
                drain_socket(client_fd);
                return;
            }

            size_t w_off = 0;
            while (w_off < (size_t)r) {
                ssize_t w = write(file_fd, buffer + w_off, (size_t)r - w_off);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    close(file_fd);
                    send_response(client_fd, S_INTERNAL_ERR, NULL, 0);
                    drain_socket(client_fd);
                    return;
                }
                w_off += (size_t)w;
            }
            bytes_to_go -= (size_t)r;
        }


        close(file_fd);


        if (created) {
            send_response(client_fd, S_CREATED, NULL, 0);
        } else {
            send_response(client_fd, S_OK, NULL, 0);
        }
        drain_socket(client_fd);
        return;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, ERR_PORT);
        return 1;
    }

    // Parse port number
    char *endptr = NULL;
    long portval = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || portval < 1 || portval > 65535) {
        fprintf(stderr, ERR_PORT);
        return 1;
    }
    Listener_Socket_t *ls = ls_new((int)portval);
    if (!ls) {
        fprintf(stderr, ERR_PORT);
        return 1;
    }
    while (1) {
        int client_fd = ls_accept(ls);
        if (client_fd < 0) {
            continue;
        }
        // process
        handle_connection(client_fd);
        close(client_fd);
    }
    ls_delete(&ls);
    return 0;
}
