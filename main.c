#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <glob.h>
#include <libgen.h>
#include <fnmatch.h>
#include <limits.h>

#include "sha1.h"

#define TRUE 1
#define FALSE 0

#define SHA1_LEN 40
#define HASH_LEN SHA1_LEN

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
                if (c == token) {
                    in_string = TRUE;
                    continue;
                }

                *c = '\0';
                return len;
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

        if (strcasecmp(hash, token) == 0) {
            return 0;
        }
    }
}

static int find_rom_canonical_name(char* hash, char* game_name,
                                   size_t max_len) {
    // TODO: Error handling
    int i;
    int fd;
    int offs;
    char* dat_path;
    char* dat_name;
    glob_t glb;
    glob("db/*.dat", GLOB_NOSORT, NULL, &glb);
    for (i = 0; i < glb.gl_pathc; i++) {
        dat_path = glb.gl_pathv[i];
        dat_name = basename(dat_path);
        offs = strchr(dat_name, '.') - dat_name + 1;
        memcpy(game_name, dat_name, offs);

        fd = open(dat_path, O_RDONLY);
        if (find_hash(fd, hash, game_name + offs,
                      max_len - offs) == 0) {
            close(fd);
            return 0;
        }

        close(fd);
    }
    return -1;
}

static int get_sha1(char* path, char* result) {
    int fd;
    int rv;
    int buff_len = 4096;
    char buff[buff_len];
    SHA1Context sha;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }

    SHA1Reset(&sha);
    rv = 1;
    while (rv > 0) {
        rv = read(fd, buff, buff_len);
        if (rv < 0) {
            close(fd);
            return -errno;
        }

        SHA1Input(&sha, buff, rv);
    }

    if (!SHA1Result(&sha)) {
        return -1;
    }

    sprintf(result, "%08X%08X%08X%08X%08X",
           sha.Message_Digest[0],
           sha.Message_Digest[1],
           sha.Message_Digest[2],
           sha.Message_Digest[3],
           sha.Message_Digest[4]);
    return 0;
}

struct RunInfo {
    char core[50];
    int multitap;
};

static int get_run_info(struct RunInfo* info, char* game_name) {
    int fd = open("./launch.conf", O_RDONLY);
    int rv;
    char token[MAX_TOKEN_LEN];
    if (fd < 0) {
        return -errno;
    }

    memset(info, 0, sizeof(struct RunInfo));

    while (TRUE) {
        if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
            goto clean;
        }

        if (fnmatch(token, game_name, 0) != 0) {
            if ((rv = find_token(fd, ";")) < 0) {
                goto clean;
            }
            continue;
        }

        if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
            goto clean;
        }

        strncpy(info->core, token, 50);
        break;
    }
    rv = 0;
clean:
    close(fd);
    return rv;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return -1;
    }

    char game_name[MAX_TOKEN_LEN];
    char* path = argv[1];
    char hash[HASH_LEN + 1];
    struct RunInfo info;
    int rv;
    int fd = -1;

    if ((rv = get_sha1(path, hash)) < 0) {
        fprintf(stderr, "Could not calculate hash: %s\n", strerror(-rv));
    }

    if (find_rom_canonical_name(hash, game_name, MAX_TOKEN_LEN) < 0) {
        //TODO: Fallback to suffix hueristics
        printf("Could not detect rom with hash `%s`\n", hash);
        return -1;
    }

    printf("Game is `%s`\n", game_name);
    if ((rv = get_run_info(&info, game_name)) < 0) {
        fprintf(stderr, "Could not find sutable core: %s\n", strerror(-rv));
        return -1;
    }

    char core_path[PATH_MAX];
    sprintf(core_path, "/usr/local/lib/libretro/libretro-%s.so", info.core);

    char* retro_argv[] = {"retroarch",
                          "-L", core_path,
                          path, NULL};
    execvp(retro_argv[0], retro_argv);
    fprintf(stderr, "Could not launch retroarch: %s", strerror(errno));

    return errno;
}
