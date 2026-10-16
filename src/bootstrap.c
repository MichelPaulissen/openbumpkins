/* SPDX-License-Identifier: GPL-3.0-only */
/* Windows-side startup for the Wine bridge. Registry values follow pinned bb-patch. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int registry_string(HKEY key, const char *name, const char *value) {
    return RegSetValueExA(key, name, 0, REG_SZ, (const BYTE *)value,
                          (DWORD)strlen(value) + 1) == ERROR_SUCCESS;
}

static int install_configuration(void) {
    char location[32768], value[32768];
    DWORD length = GetCurrentDirectoryA(sizeof(location) - 2, location);
    if (!length || length >= sizeof(location) - 2 || location[1] != ':')
        return 0;
    if (location[length - 1] != '\\') {
        location[length++] = '\\';
        location[length] = '\0';
    }
    static const char *path = "SOFTWARE\\Worldweaver Productions\\Beasts and Bumpkins";
    LSTATUS status = RegDeleteKeyA(HKEY_CURRENT_USER, path);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
        return 0;
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
        return 0;
    struct { const char *name; const char *file; } files[] = {
        {"Audio", "audio.box"}, {"CD", ""}, {"Misc", "misc.box"},
        {"Missions", "missions.box"}, {"MissionText", "mistext0.box"},
        {"Speech", "speech0.box"}, {"Video", "video.box"}, {"Working", ""}
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (snprintf(value, sizeof(value), "%s%s", location, files[i].file) >= (int)sizeof(value)) {
            ok = 0;
            break;
        }
        ok &= registry_string(key, files[i].name, value);
    }
    ok &= registry_string(key, "InstallDir", location + 2);
    char drive[] = {location[0], ':', '\0'};
    ok &= registry_string(key, "InstallDrive", drive);
    ok &= registry_string(key, "Link", "");
    DWORD installed = 1;
    DWORD language = GetPrivateProfileIntA("reference", "language", 0, ".\\bumpkins.ini");
    if (language > 4)
        ok = 0;
    ok &= RegSetValueExA(key, "Installed", 0, REG_DWORD, (BYTE *)&installed,
                        sizeof(installed)) == ERROR_SUCCESS;
    ok &= RegSetValueExA(key, "Language", 0, REG_DWORD, (BYTE *)&language,
                        sizeof(language)) == ERROR_SUCCESS;
    RegCloseKey(key);

    /* Wine supplies the platform implementation; native DDrawCompat cannot initialize here. */
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\Direct3D", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return 0;
    ok &= registry_string(key, "DirectDrawRenderer", "gdi");
    RegCloseKey(key);
    return ok;
}

/* Quote one Windows CRT argument, including trailing backslashes and embedded quotes. */
static int append_argument(char *line, size_t capacity, size_t *used, const char *argument) {
#define APPEND(ch) do { if (*used + 1 >= capacity) return 0; line[(*used)++] = (ch); } while (0)
    if (*used)
        APPEND(' ');
    APPEND('"');
    const char *p = argument;
    for (;;) {
        size_t slashes = 0;
        while (*p == '\\') {
            slashes++;
            p++;
        }
        size_t count = (*p == '\0' || *p == '"') ? 2 * slashes : slashes;
        while (count--)
            APPEND('\\');
        if (!*p)
            break;
        if (*p == '"')
            APPEND('\\');
        APPEND(*p++);
    }
    APPEND('"');
    line[*used] = '\0';
    return 1;
#undef APPEND
}

int main(int argc, char **argv) {
    if (!install_configuration()) {
        fprintf(stderr, "Could not configure the game in this Wine prefix (error %lu).\n", GetLastError());
        return 1;
    }
    char line[32768];
    size_t used = 0;
    if (!append_argument(line, sizeof(line), &used, "BEASTS.EXE"))
        return 1;
    for (int i = 1; i < argc; i++) {
        if (!append_argument(line, sizeof(line), &used, argv[i])) {
            fprintf(stderr, "Game command line is too long.\n");
            return 1;
        }
    }
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessA("BEASTS.EXE", line, NULL, NULL, TRUE, 0, NULL, NULL,
                        &startup, &process)) {
        fprintf(stderr, "Could not start BEASTS.EXE (error %lu).\n", GetLastError());
        return 1;
    }
    CloseHandle(process.hThread);
    DWORD code = 1;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}
