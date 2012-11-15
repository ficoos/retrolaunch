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
#include "parser.h"
#include "cd_detect.h"

#include "log.h"

#define SHA1_LEN 40
#define HASH_LEN SHA1_LEN

static int find_hash(int fd, const char* hash, char* game_name, size_t max_len) {
    ssize_t rv;
    char token[MAX_TOKEN_LEN];
    while (1) {
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

static int find_rom_canonical_name(const char* hash, char* game_name,
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

static int get_sha1(const char* path, char* result) {
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
    int dualanalog;
};

static int get_run_info(struct RunInfo* info, char* game_name) {
    int fd = open("./launch.conf", O_RDONLY);
    int rv;
    char token[MAX_TOKEN_LEN];
    if (fd < 0) {
        return -errno;
    }

    memset(info, 0, sizeof(struct RunInfo));

    while (1) {
        if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
            goto clean;
        }

        if (fnmatch(token, game_name, 0) != 0) {
            if ((rv = find_token(fd, ";")) < 0) {
                goto clean;
            }
            continue;
        }

        LOG_DEBUG("Matched rule '%s'", token);

        if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
            goto clean;
        }

        break;
    }

    strncpy(info->core, token, 50);
    info->multitap = 0;
    info->dualanalog = 0;

    if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
        goto clean;
    }

    while (strcmp(token, ";") != 0) {
        if (strcmp(token, "multitap") == 0) {
            info->multitap = 1;
        } else if (strcmp(token, "dualanalog") == 0) {
            info->dualanalog = 1;
        }

        if ((rv = get_token(fd, token, MAX_TOKEN_LEN)) < 0) {
            goto clean;
        }

    }
    rv = 0;
clean:
    close(fd);
    return rv;
}

char* SUFFIX_MATCH[] = {
    ".nes", "nes",
    ".gen", "smd",
    ".smd", "smd",
    ".bin", "smd",
    ".sfc", "snes",
    ".smc", "snes",
    ".gg", "gg",
    ".sms", "sms",
    ".pce", "pce",
    ".gba", "gba",
    ".gb", "gb",
    ".gbc", "gbc",
    ".nds", "nds",
    NULL
};

static int detect_rom_game(const char* path, char* game_name,
                           size_t max_len) {
    char hash[HASH_LEN + 1];
    int rv;
    char* suffix = strrchr(path, '.');
    char** tmp_suffix;
    if ((rv = get_sha1(path, hash)) < 0) {
        LOG_WARN("Could not calculate hash: %s", strerror(-rv));
    }

    if (find_rom_canonical_name(hash, game_name, max_len) < 0) {
        LOG_DEBUG("Could not detect rom with hash `%s` guessing", hash);

        for (tmp_suffix = SUFFIX_MATCH; *tmp_suffix != NULL;
             tmp_suffix += 2) {
            if (strcasecmp(suffix, *tmp_suffix) == 0) {
                snprintf(game_name, max_len, "%s.<unknown>",
                         *(tmp_suffix + 1));
                return 0;
            }
        }
        return -EINVAL;
    }

    return 0;
}

static int detect_game(const char* path, char* game_name, size_t max_len) {
    if ((strcasecmp(path + strlen(path) - 4, ".cue") == 0) ||
        (strcasecmp(path + strlen(path) - 4, ".m3u") == 0)) {
        LOG_INFO("Starting CD game detection...");
        return detect_cd_game(path, game_name, max_len);
    } else {
        LOG_INFO("Starting rom game detection...");
        return detect_rom_game(path, game_name, max_len);
    }
}

static int run_retroarch(const char* path, const struct RunInfo* info) {
    char core_path[PATH_MAX];
    sprintf(core_path, "./cores/libretro-%s.so", info->core);
    char* retro_argv[30] = {"retroarch",
                          "-L", core_path};
    int argi = 3;
    if (info->multitap) {
        retro_argv[argi] = "-4";
        argi++;
        LOG_INFO("Game supports multitap");
    }

    if (info->dualanalog) {
        retro_argv[argi] = "-A";
        argi ++;
        retro_argv[argi] = "1";
        argi ++;
        retro_argv[argi] = "-A";
        argi ++;
        retro_argv[argi] = "2";
        argi ++;
        LOG_INFO("Game supports the dualshock controller");
    }

    retro_argv[argi] = strdup(path);
    argi ++;
    retro_argv[argi] = NULL;
    execvp(retro_argv[0], retro_argv);
    return -errno;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return -1;
    }

    char game_name[MAX_TOKEN_LEN];
    char* path = argv[1];
    struct RunInfo info;
    int rv;
    int fd = -1;

    LOG_INFO("Analyzing '%s'", path);
    if ((rv = detect_game(path, game_name, MAX_TOKEN_LEN)) < 0) {
        LOG_WARN("Could not detect game: %s", strerror(-rv));
        return -rv;
    }

    LOG_INFO("Game is `%s`", game_name);
    if ((rv = get_run_info(&info, game_name)) < 0) {
        LOG_WARN("Could not find sutable core: %s", strerror(-rv));
        return -1;
    }

    LOG_DEBUG("Usinge libretro core '%s'", info.core);
    LOG_INFO("Launching '%s'", path);

    rv = run_retroarch(path, &info);
    LOG_WARN("Could not launch retroarch: %s", strerror(-rv));
    return -rv;
}
