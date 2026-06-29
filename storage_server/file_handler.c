#include "file_handler.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int scan_storage_directory(const char *directory, SS_Registration_Packet *packet) {
    if (!directory || !packet) return -1;

    DIR *dir = opendir(directory);
    if (!dir) return -1;

    packet->file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        if (packet->file_count >= MAX_FILES) break;
        snprintf(packet->filenames[packet->file_count], MAX_FILENAME, "%s", entry->d_name);
        packet->file_count++;
    }

    closedir(dir);
    return 0;
}

int read_file_into_buffer(const char *directory, const char *filename, char **buffer, long *length) {
    if (!directory || !filename || !buffer || !length) return -1;
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", directory, filename);

    FILE *fp = fopen(full_path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[read_bytes] = '\0';

    *buffer = buf;
    *length = (long)read_bytes;
    return 0;
}