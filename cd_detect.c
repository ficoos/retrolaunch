#include "parser.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <libgen.h>
#include <stdlib.h>

#define MAGIC_LEN 16

struct MagicEntry {
    char* system_name;
    char* magic;
};

static struct MagicEntry MAGIC_NUMBERS[] = {
    {"ps1", "\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x02\x00\x02\x00"},
    {"pcecd", "\x82\xb1\x82\xcc\x83\x76\x83\x8d\x83\x4f\x83\x89\x83\x80\x82\xcc\x92"},
    {"scd", "\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x02\x00\x01\x53"},
    {NULL, NULL}
};

static int find_first_data_track(const char* cue_path, off_t* offset,
                                 char* track_path, size_t max_len) {
    int rv;
    int fd = open(cue_path, O_RDONLY);
    char tmp_token[MAX_TOKEN_LEN];
    int m, s, f;
    char* cue_path_copy;
    char* cue_dir;
    cue_path_copy = strdup(cue_path);
    cue_dir = dirname(cue_path_copy);

    fd = open(cue_path, O_RDONLY);
    if (fd < 0) {
        printf("Could not open CUE file: %s\n", strerror(errno));
        rv -errno;
        goto free_path_copy;
    }

    printf("Parsing CUE file...\n");

    while (get_token(fd, tmp_token, MAX_TOKEN_LEN) > 0) {
        if (strcmp(tmp_token, "FILE") == 0) {
            get_token(fd, tmp_token, MAX_TOKEN_LEN);
            snprintf(track_path, max_len, "%s/%s",
                    cue_dir, tmp_token);

        } else if (strcasecmp(tmp_token, "TRACK") == 0) {
            get_token(fd, tmp_token, MAX_TOKEN_LEN);
            get_token(fd, tmp_token, MAX_TOKEN_LEN);
            if (strcasecmp(tmp_token, "AUDIO") == 0) {
                continue;
            }

            find_token(fd, "INDEX");
            get_token(fd, tmp_token, MAX_TOKEN_LEN);
            get_token(fd, tmp_token, MAX_TOKEN_LEN);
            if (sscanf(tmp_token, "%02d:%02d:%02d", &m, &s, &f) < 3) {
                printf("Error parsing time stamp '%s'\n", tmp_token);
                return -errno;
            }
            *offset = ((m * 60) * (s * 75) * f) * 25;

            printf("Found 1st data track on file '%s+%d'\n",
                    track_path, *offset);

            rv = 0;
            goto clean;
        }
    }

    rv = -EINVAL;

clean:
    close(fd);
free_path_copy:
    free(cue_path_copy);
    return rv;
}

static int detect_system(const char* cue_path, char** system_name) {
    char track_path[PATH_MAX];
    off_t offset;
    int rv;
    char magic[MAGIC_LEN];
    int fd;
    struct MagicEntry entry;
    int i;

    rv = find_first_data_track(cue_path, &offset, track_path, PATH_MAX);
    if (rv < 0) {
        printf("Could not find valid data track: %s\n", strerror(-rv));
        return rv;
    }

    printf("Reading 1st data track...\n");
    fd = open(track_path, O_RDONLY);
    if (fd < 0) {
        rv = -errno;
        goto clean;
    }

    if (pread(fd, magic, MAGIC_LEN, offset) < MAGIC_LEN) {
        rv = -errno;
        goto clean;
    }

    for (i = 0; MAGIC_NUMBERS[i].system_name != NULL; i++) {
        if (memcmp(MAGIC_NUMBERS[i].magic, magic, MAGIC_LEN) == 0) {
            *system_name = MAGIC_NUMBERS[i].system_name;
            rv = 0;
            goto clean;
        }
    }

    printf("Could not find compatible system\n");
    rv = -EINVAL;
clean:
    close(fd);
    return rv;
}

int detect_cd_game(const char* cue_path, char* game_name, size_t max_len) {
    char* system_name;
    int rv;

    if ((rv = detect_system(cue_path, &system_name)) < 0) {
        return rv;
    }

    snprintf(game_name, max_len, "%s.<unknown>", system_name);
    return 0;
}

