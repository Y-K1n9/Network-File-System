#include "storage_registry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTRY_BUCKETS 256

typedef struct FileMapNode {
    char filename[MAX_FILENAME];
    StorageServer *servers[MAX_REPLICAS];
    int32_t server_count;
    struct FileMapNode *next;
} FileMapNode;

static FileMapNode *buckets[REGISTRY_BUCKETS];
static StorageServer active_servers[MAX_STORAGE_SERVERS];
static int32_t active_server_count = 0;

static ClientEntry active_clients[MAX_CLIENTS];
static int32_t active_client_count = 0;

pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned long hash_file(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

void registry_init(void) {
    pthread_mutex_lock(&registry_mutex);
    active_server_count = 0;
    active_client_count = 0;
    memset(active_servers, 0, sizeof(active_servers));
    memset(active_clients, 0, sizeof(active_clients));
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

StorageServer *registry_register_ss(const char *ip, int nm_port, int client_port) {
    if (!ip) return NULL;
    pthread_mutex_lock(&registry_mutex);

    // Check if already registered
    for (int i = 0; i < active_server_count; ++i) {
        if (strcmp(active_servers[i].ip, ip) == 0 &&
            active_servers[i].client_port == client_port) {
            active_servers[i].status = SS_STATUS_ONLINE;
            active_servers[i].last_heartbeat = time(NULL);
            StorageServer *res = &active_servers[i];
            pthread_mutex_unlock(&registry_mutex);
            return res;
        }
    }

    if (active_server_count >= MAX_STORAGE_SERVERS) {
        pthread_mutex_unlock(&registry_mutex);
        return NULL;
    }

    StorageServer *ss = &active_servers[active_server_count];
    ss->id = active_server_count + 1;
    strncpy(ss->ip, ip, MAX_IP_LEN - 1);
    ss->ip[MAX_IP_LEN - 1] = '\0';
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->status = SS_STATUS_ONLINE;
    ss->last_heartbeat = time(NULL);
    active_server_count++;

    pthread_mutex_unlock(&registry_mutex);
    return ss;
}

int registry_add_file(const char *filename, StorageServer *ss) {
    if (!filename || !ss) return -1;
    pthread_mutex_lock(&registry_mutex);
    unsigned long idx = hash_file(filename) % REGISTRY_BUCKETS;
    FileMapNode *curr = buckets[idx];
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            for (int j = 0; j < curr->server_count; ++j) {
                if (curr->servers[j] == ss) {
                    pthread_mutex_unlock(&registry_mutex);
                    return 0;
                }
            }
            if (curr->server_count < MAX_REPLICAS) {
                curr->servers[curr->server_count++] = ss;
                pthread_mutex_unlock(&registry_mutex);
                return 0;
            }
            pthread_mutex_unlock(&registry_mutex);
            return -2;
        }
        curr = curr->next;
    }

    FileMapNode *node = (FileMapNode *)calloc(1, sizeof(FileMapNode));
    if (!node) {
        pthread_mutex_unlock(&registry_mutex);
        return -1;
    }
    strncpy(node->filename, filename, MAX_FILENAME - 1);
    node->filename[MAX_FILENAME - 1] = '\0';
    node->servers[0] = ss;
    node->server_count = 1;
    node->next = buckets[idx];
    buckets[idx] = node;

    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

int registry_find_file(const char *filename, StorageServer **out_servers, int32_t *out_count) {
    if (!filename || !out_servers || !out_count) return 0;
    pthread_mutex_lock(&registry_mutex);
    unsigned long idx = hash_file(filename) % REGISTRY_BUCKETS;
    FileMapNode *curr = buckets[idx];
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            *out_count = curr->server_count;
            for (int i = 0; i < curr->server_count; ++i) {
                out_servers[i] = curr->servers[i];
            }
            pthread_mutex_unlock(&registry_mutex);
            return 1;
        }
        curr = curr->next;
    }
    *out_count = 0;
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

void registry_update_heartbeat(int client_port) {
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < active_server_count; ++i) {
        if (active_servers[i].client_port == client_port) {
            active_servers[i].status = SS_STATUS_ONLINE;
            active_servers[i].last_heartbeat = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
}

void registry_check_timeouts(int timeout_seconds) {
    pthread_mutex_lock(&registry_mutex);
    time_t now = time(NULL);
    for (int i = 0; i < active_server_count; ++i) {
        if (active_servers[i].status == SS_STATUS_ONLINE) {
            if (now - active_servers[i].last_heartbeat > timeout_seconds) {
                active_servers[i].status = SS_STATUS_OFFLINE;
                printf("[NM] Storage Server on port %d timed out (marked OFFLINE)\n", active_servers[i].client_port);
            }
        }
    }
    pthread_mutex_unlock(&registry_mutex);
}

int registry_register_client(const char *username, const char *ip) {
    if (!username || strlen(username) == 0) return -1;
    pthread_mutex_lock(&registry_mutex);

    // Check if already registered
    for (int i = 0; i < active_client_count; ++i) {
        if (strcmp(active_clients[i].username, username) == 0) {
            active_clients[i].active = 1;
            if (ip) strncpy(active_clients[i].ip, ip, MAX_IP_LEN - 1);
            pthread_mutex_unlock(&registry_mutex);
            return 0;
        }
    }

    if (active_client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&registry_mutex);
        return -1;
    }

    ClientEntry *c = &active_clients[active_client_count];
    strncpy(c->username, username, MAX_USERNAME - 1);
    c->username[MAX_USERNAME - 1] = '\0';
    if (ip) strncpy(c->ip, ip, MAX_IP_LEN - 1);
    c->ip[MAX_IP_LEN - 1] = '\0';
    c->active = 1;
    active_client_count++;

    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

int registry_is_client_registered(const char *username) {
    if (!username) return 0;
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < active_client_count; ++i) {
        if (strcmp(active_clients[i].username, username) == 0 && active_clients[i].active) {
            pthread_mutex_unlock(&registry_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}