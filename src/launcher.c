/* SPDX-License-Identifier: GPL-3.0-only */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *name) {
    printf("Usage: %s [--game-dir DIRECTORY] [--reference] [-- GAME_ARGUMENTS...]\n", name);
}

int main(int argc, char **argv) {
    const char *directory = "work/bridged";
    int first_game_argument = argc;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-dir") && i + 1 < argc)
            directory = argv[++i];
        else if (!strcmp(argv[i], "--reference")) {
            if (setenv("BB_BRIDGE_DISABLE", "1", 1))
                return 1;
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--")) {
            first_game_argument = i + 1;
            break;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    char executable[PATH_MAX], bootstrap[PATH_MAX], windows_bootstrap[PATH_MAX + 2], game[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || length == (ssize_t)sizeof(executable) - 1)
        return 1;
    executable[length] = '\0';
    char *separator = strrchr(executable, '/');
    if (!separator)
        return 1;
    *separator = '\0';
    if (snprintf(bootstrap, sizeof(bootstrap), "%s/../windows/bumpkins-bootstrap.exe", executable) >=
        (int)sizeof(bootstrap))
        return 1;
    if (access(bootstrap, R_OK) != 0 || !realpath(bootstrap, windows_bootstrap + 2) ||
        !realpath(directory, game) || chdir(game) != 0 ||
        access("BEASTS.EXE", R_OK) != 0) {
        fprintf(stderr, "Build both targets and prepare the game directory first: %s\n", strerror(errno));
        return 1;
    }
    /* Wine's default Z: drive maps the host root; Explorer needs a Windows path. */
    windows_bootstrap[0] = 'Z';
    windows_bootstrap[1] = ':';
    for (char *p = windows_bootstrap + 2; *p; p++) {
        if (*p == '/')
            *p = '\\';
    }

    const char *existing = getenv("WINEDLLOVERRIDES");
    const char *suffix = ";ddraw=b";
    size_t size = (existing ? strlen(existing) : 0) + strlen(suffix) + 1;
    char *overrides = malloc(size);
    if (!overrides)
        return 1;
    snprintf(overrides, size, "%s%s", existing ? existing : "", suffix);
    if (setenv("WINEDLLOVERRIDES", overrides, 1))
        return 1;
    free(overrides);

    size_t arguments = (size_t)(argc - first_game_argument);
    char **command = calloc(arguments + 5, sizeof(*command));
    if (!command)
        return 1;
    command[0] = "wine";
    command[1] = "explorer.exe";
    /* The original 8-bit renderer requires its display to match the game size. */
    command[2] = "/desktop=Bumpkins,640x480";
    command[3] = windows_bootstrap;
    for (size_t i = 0; i < arguments; i++)
        command[i + 4] = argv[first_game_argument + (int)i];
    execvp(command[0], command);
    fprintf(stderr, "Could not start Wine: %s\n", strerror(errno));
    free(command);
    return 1;
}
