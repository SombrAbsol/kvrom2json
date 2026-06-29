/*
 * Convert KVROM files into JSON.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "parse.h"
#include "utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/*
 * Wrappers for MSVC-deprecated CRT functions.
 */
#ifdef _WIN32
static FILE *xfopen(const char *path, const char *mode)
{
    FILE *f = NULL;
    fopen_s(&f, path, mode);
    return f;
}
#else
#define xfopen(path, mode) fopen((path), (mode))
#endif

/*
 * Load a file, parse it as KVROM, and write a .json file next to it.
 * If 'sort' is true the JSON keys are sorted before writing.
 */
static void process_bytes_file(
    const char *input_path, bool sort, long known_size)
{
    uint8_t *data;
    size_t size;
    if (!file_load(input_path, &data, &size, known_size)) {
        fprintf(stderr, "Cannot read: '%s'\n", input_path);
        return;
    }

    // build the output path in a fixed-size buffer
    char output[4096];
    snprintf(output, sizeof(output), "%s", input_path);

    // build the output path, replacing or appending the .json extension
    const char *ext = strrchr(input_path, '.');
    if (ext) {
        snprintf(output,
            sizeof(output),
            "%.*s.json",
            (int)(ext - input_path),
            input_path);
    } else {
        snprintf(output, sizeof(output), "%s.json", input_path);
    }

    // reset the global entry store
    entries_clear();

    if (!kvrom_parse(data, size)) {
        fprintf(stderr, "Invalid magic: '%s'\n", input_path);
        free(data);
        return;
    }
    free(data);

    FILE *json = xfopen(output, "w");
    if (!json) {
        fprintf(stderr, "Cannot open output: '%s'\n", output);
        entries_clear();
        return;
    }

    json_write_entries(json, sort);
    fclose(json);
    entries_clear();

    printf("Created '%s'\n", output);
}

/*
 * If 'path' is a regular file, convert it directly.
 * If 'path' is a directory, find every regular file inside and attempt to
 * convert each.
 */
#ifdef _WIN32
static bool process_path(const char *path, bool sort)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        // enumerate all files in the directory
        char search[MAX_PATH];
        snprintf(search, sizeof(search), "%s\\*", path);

        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(search, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        do {
            // skip the current-directory and parent-directory entries
            if (strcmp(fd.cFileName, ".") == 0
                || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            // skip sub-directories, only process regular files
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            // reconstruct the full path by joining the directory and filename
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
            // nFileSizeLow suffices for files under 4 GB
            long sz = (long)fd.nFileSizeLow;
            process_bytes_file(full, sort, sz);
        } while (FindNextFileA(h, &fd)); // advance to the next match
        FindClose(h); // release the search handle even if no files matched
    } else {
        process_bytes_file(path, sort, 0);
    }

    return true;
}
#else
static bool process_path(const char *path, bool sort)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return false;
        }
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            // skip the current-directory and parent-directory entries cheaply
            if (ent->d_name[0] == '.'
                && (ent->d_name[1] == '\0'
                    || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
                continue;
            }
            // build the full path by joining directory and filename with '/'
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
            // stat to confirm it is a regular file and obtain the file size
            struct stat est;
            if (stat(full, &est) != 0) {
                fprintf(stderr, "Cannot stat: '%s'\n", full);
                continue;
            }
            if (!S_ISREG(est.st_mode)) {
                continue;
            }
            // pass the size so file_load can skip the fseek/ftell sequence
            process_bytes_file(full, sort, (long)est.st_size);
        }
        closedir(dir);
    } else {
        // forward st.st_size to skip the seek sequence
        process_bytes_file(path, sort, (long)st.st_size);
    }

    return true;
}
#endif

/*
 * Command-line interface.
 */
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // ensure UTF-8 output on Windows
#endif
    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("kvrom2json - KVROM text converter for Pokémon Trading Card "
               "Game Pocket\n");
        printf("Copyright (c) 2026 SombrAbsol\n\n");
        printf("Usage:\n");
        printf("  %s <infile|indir>            convert\n", argv[0]);
        printf("  %s -s|--sort <infile|indir>  convert+sort keys\n", argv[0]);
        printf("  %s -h|--help                 show this help\n", argv[0]);
        return EXIT_SUCCESS;
    }

    bool sort = false;
    const char *path = NULL;

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3) {
        if (!strcmp(argv[1], "-s") || !strcmp(argv[1], "--sort")) {
            sort = true;
            path = argv[2];
        } else {
            fprintf(stderr, "Unknown option: '%s'\n", argv[1]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "Invalid arguments\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!process_path(path, sort)) {
        fprintf(stderr, "Invalid path: '%s'\n", path);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
