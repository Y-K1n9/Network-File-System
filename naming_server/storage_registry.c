#include "storage_registry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTRY_BUCKETS 256

typedef struct FileMapNode {
    char filename[MAX_FILENAME];
    SS_Registration_Packet packet;
    struct FileMapNode *next;
} FileMapNode;

static FileMapNode *buckets[REGISTRY_BUCKETS];
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned long hash_file(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++) != 0) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

void registry_init(void) {
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < REGISTRY_BUCKETS; ++i) {
        FileMapNode *node = buckets[i];
        while (node) {
            FileMapNode *next = node->next;
            free(node);
            node = next;
        }
        buckets[i] = NULL;
    }
    pthread_mutex_unlock(&registry_mutex);
}

void registry_upsert(const SS_Registration_Packet *packet) {
    if (!packet) return;
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < packet->file_count; ++i) {
        const char *filename = packet->filenames[i];
        unsigned long idx = hash_file(filename) % REGISTRY_BUCKETS;

        FileMapNode *curr = buckets[idx];
        while (curr) {
            if (strcmp(curr->filename, filename) == 0) {
                curr->packet = *packet;
                goto next_file;
            }
            curr = curr->next;
        }

        FileMapNode *node = (FileMapNode *)calloc(1, sizeof(FileMapNode));
        if (!node) goto next_file;
        strncpy(node->filename, filename, MAX_FILENAME - 1);
        node->packet = *packet;
        node->next = buckets[idx];
        buckets[idx] = node;
    next_file:
        ;
    }
    pthread_mutex_unlock(&registry_mutex);
}

int registry_find_file(const char *filename, SS_Registration_Packet *out) {
    if (!filename || !out) return 0;
    pthread_mutex_lock(&registry_mutex);
    unsigned long idx = hash_file(filename) % REGISTRY_BUCKETS;
    FileMapNode *curr = buckets[idx];
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            *out = curr->packet;
            pthread_mutex_unlock(&registry_mutex);
            return 1;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

void registry_list_files(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < REGISTRY_BUCKETS; ++i) {
        FileMapNode *curr = buckets[i];
        while (curr) {
            strncat(buffer, curr->filename, buffer_size - strlen(buffer) - 1);
            strncat(buffer, "\n", buffer_size - strlen(buffer) - 1);
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
}