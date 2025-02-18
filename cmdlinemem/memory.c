#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>

#define MAX_LINE_LEN 4096
#define MAX_FILENAME 255
#define IO_BUF_SIZE  65536

static ssize_t read_line(int fd, char *buf, size_t buf_size) {
    if (buf_size == 0)
        return -1;
    size_t idx = 0;
    while (1) {
        char c;
        ssize_t rd = read(fd, &c, 1);
        if (rd < 0)
            return -1;
        else if (rd == 0)
            return (idx == 0) ? 0 : -1;
        else {
            if (c == '\n') {
                buf[idx] = '\0';
                return (ssize_t) idx;
            }
            if (idx < buf_size - 1)
                buf[idx++] = c;
            else
                return -1;
        }
    }
}

static int is_valid_filename(const char *filename) {
    size_t len = strlen(filename);
    if (len == 0 || len > MAX_FILENAME)
        return 0;
    for (size_t i = 0; i < len; i++) {
        if (filename[i] == '/')
            return 0;
    }
    struct stat st;
    if (stat(filename, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
    }
    return 1;
}

static int parse_content_length(const char *str, size_t *out_len) {
    if (str[0] == '\0')
        return 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (!isdigit((unsigned char) str[i]))
            return 0;
    }
    unsigned long long val = strtoull(str, NULL, 10);
    if (val > (unsigned long long) SIZE_MAX)
        return 0;
    *out_len = (size_t) val;
    return 1;
}

static int safe_write(int fd, const char *buf, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t wr = write(fd, buf + total_written, count - total_written);
        if (wr < 0)
            return -1;
        if (wr == 0)
            return -1;
        total_written += (size_t) wr;
    }
    return 0;
}

static ssize_t read_exact(int fd, char *buf, size_t count) {
    size_t total_read = 0;
    while (total_read < count) {
        ssize_t rd = read(fd, buf + total_read, count - total_read);
        if (rd < 0)
            return -1;
        if (rd == 0)
            break;
        total_read += (size_t) rd;
    }
    return (ssize_t) total_read;
}

static int check_for_extra_input_after_get(void) {
    char c;
    ssize_t rd = read(STDIN_FILENO, &c, 1);
    if (rd < 0)
        return -1;
    if (rd > 0)
        return -1;
    return 0;
}

static void print_to_stderr(const char *msg) {
    safe_write(STDERR_FILENO, msg, strlen(msg));
}

static void print_to_stdout(const char *msg) {
    safe_write(STDOUT_FILENO, msg, strlen(msg));
}

int main(void) {
    char line_buf[MAX_LINE_LEN];
    ssize_t nread = read_line(STDIN_FILENO, line_buf, sizeof(line_buf));
    if (nread <= 0) {
        print_to_stderr("Invalid Command\n");
        return 1;
    }
    if (strcmp(line_buf, "get") == 0) {
        nread = read_line(STDIN_FILENO, line_buf, sizeof(line_buf));
        if (nread <= 0) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        if (!is_valid_filename(line_buf)) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        struct stat st;
        if (stat(line_buf, &st) != 0) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        if (S_ISDIR(st.st_mode)) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        if (check_for_extra_input_after_get() != 0) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        int fd = open(line_buf, O_RDONLY);
        if (fd < 0) {
            print_to_stderr("Operation Failed\n");
            return 1;
        }
        char io_buf[IO_BUF_SIZE];
        ssize_t bytes_read;
        while ((bytes_read = read(fd, io_buf, IO_BUF_SIZE)) > 0) {
            if (safe_write(STDOUT_FILENO, io_buf, (size_t) bytes_read) < 0) {
                print_to_stderr("Operation Failed\n");
                close(fd);
                return 1;
            }
        }
        if (bytes_read < 0) {
            print_to_stderr("Operation Failed\n");
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    } else if (strcmp(line_buf, "set") == 0) {
        nread = read_line(STDIN_FILENO, line_buf, sizeof(line_buf));
        if (nread <= 0) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        char filename[MAX_LINE_LEN];
        strncpy(filename, line_buf, sizeof(filename));
        filename[sizeof(filename) - 1] = '\0';
        if (!is_valid_filename(filename)) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        nread = read_line(STDIN_FILENO, line_buf, sizeof(line_buf));
        if (nread <= 0) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        size_t content_length = 0;
        if (!parse_content_length(line_buf, &content_length)) {
            print_to_stderr("Invalid Command\n");
            return 1;
        }
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            print_to_stderr("Operation Failed\n");
            return 1;
        }
        size_t remaining = content_length;
        char io_buf[IO_BUF_SIZE];
        while (remaining > 0) {
            size_t chunk_size = (remaining < IO_BUF_SIZE) ? remaining : IO_BUF_SIZE;
            ssize_t rd = read_exact(STDIN_FILENO, io_buf, chunk_size);
            if (rd < 0) {
                print_to_stderr("Operation Failed\n");
                close(fd);
                return 1;
            }
            if (rd == 0)
                break;
            if (safe_write(fd, io_buf, (size_t) rd) < 0) {
                print_to_stderr("Operation Failed\n");
                close(fd);
                return 1;
            }
            remaining -= (size_t) rd;
            if ((size_t) rd < chunk_size)
                break;
        }
        if (remaining > 0) {
        } else {
            char discard[IO_BUF_SIZE];
            while (1) {
                ssize_t rd = read(STDIN_FILENO, discard, IO_BUF_SIZE);
                if (rd < 0) {
                    print_to_stderr("Operation Failed\n");
                    close(fd);
                    return 1;
                }
                if (rd == 0)
                    break;
            }
        }
        close(fd);
        print_to_stdout("OK\n");
        return 0;
    } else {
        print_to_stderr("Invalid Command\n");
        return 1;
    }
}
