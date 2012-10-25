#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define TRUE 1
#define FALSE 0

#define MAX_TOKEN_LEN 255

static ssize_t get_token(int fd, char* token, size_t max_len) {
    char* c = token;
    int rv;
    ssize_t len = 0;
    int in_string = FALSE;

    while (TRUE) {
        rv = read(fd, c, 1);
        if (rv == 0) {
            return 0;
        } else if (rv < 1) {
            switch(errno) {
                case EINTR:
                case EAGAIN:
                    continue;
                default:
                    return -errno;
            }
        }

        switch (*c) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                if (c == token) {
                    continue;
                }

                if (!in_string) {
                    *c = '\0';
                    return len;
                }
                break;
            case '\"':
                in_string = (in_string == FALSE);
        }

        len++;
        c++;
        if (len == max_len) {
            *c = '\0';
            return len;
        }
    }
}

static int find_token(int fd, char* token) {
    int tmp_len = strlen(token);
    char* tmp_token = calloc(tmp_len, sizeof(char));
    while (strncmp(tmp_token, token, tmp_len) != 0) {
        if (get_token(fd, tmp_token, tmp_len) <= 0) {
            return -1;
        }
    }

    return 0;
}

static int find_hash(int fd, char* hash, char* game_name, size_t max_len) {
    ssize_t rv;
    char token[MAX_TOKEN_LEN];
    while (TRUE) {
        if (find_token(fd, "game") < 0) {
            return -1;
        }

        if (find_token(fd, "name") < 0) {
            return -1;
        }

        if (get_token(fd, game_name, max_len) < 0) {
            return -1;
        }

        if (find_token(fd, "sha1") < 0) {
            return -1;
        }

        if (get_token(fd, token, MAX_TOKEN_LEN) < 0) {
            return -1;
        }

        if (strcpy(hash, token) == 0) {
            return 0;
        }
    }
}
