/*
 * storage_server/file_handler.c
 *
 * Preserved from Y-K1n9 (scan_storage_directory, read_file_into_buffer),
 * extended with create_empty_file and count_file_stats for Feature 1.
 */

#include "file_handler.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ─── scan_storage_directory (Y-K1n9 original) ───────────────────────────── */
int scan_storage_directory(const char *directory,
                             SS_Registration_Packet *packet) {
    if (!directory || !packet) return -1;

    DIR *dir = opendir(directory);
    if (!dir) return -1;

    packet->file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 directory, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;   /* skip sub-directories */

        if (packet->file_count >= MAX_FILES) break;
        strncpy(packet->filenames[packet->file_count],
                entry->d_name, MAX_FILENAME - 1);
        packet->file_count++;
    }
    closedir(dir);
    return 0;
}

/* ─── read_file_into_buffer (Y-K1n9 original) ────────────────────────────── */
int read_file_into_buffer(const char *directory, const char *filename,
                            char **buffer, long *length) {
    if (!directory || !filename || !buffer || !length) return -1;

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", directory, filename);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    *length = ftell(f);
    rewind(f);

    *buffer = malloc((size_t)(*length) + 1);
    if (!*buffer) { fclose(f); return -1; }

    size_t nread = fread(*buffer, 1, (size_t)*length, f);
    (*buffer)[nread] = '\0';
    *length = (long)nread;
    fclose(f);
    return 0;
}

/* ─── create_empty_file (new — Feature 1) ────────────────────────────────── */
int create_empty_file(const char *directory, const char *filename) {
    if (!directory || !filename) return -1;

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", directory, filename);

    /* Reject if it already exists */
    struct stat st;
    if (stat(path, &st) == 0) return -1;   /* already there */

    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fclose(f);
    return 0;
}

/* ─── count_file_stats (new — Feature 4 VIEW -l) ─────────────────────────── */
int count_file_stats(const char *directory, const char *filename,
                      int *words, int *chars, long *bytes) {
    *words = 0; *chars = 0; *bytes = 0;

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", directory, filename);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int c, in_word = 0;
    while ((c = fgetc(f)) != EOF) {
        (*bytes)++;
        if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
            (*chars)++;
            if (!in_word) { (*words)++; in_word = 1; }
        } else {
            in_word = 0;
        }
    }
    fclose(f);
    return 0;
}
