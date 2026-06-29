#ifndef STORAGE_REGISTRY_H
#define STORAGE_REGISTRY_H

#include "../common/protocols.h"
#include <stddef.h>
#include <pthread.h>
#include <time.h>

#define MAX_STORAGE_SERVERS 16
#define MAX_REPLICAS 3
#define MAX_CLIENTS 32

typedef enum {
    SS_STATUS_OFFLINE = 0,
    SS_STATUS_ONLINE = 1
} SSStatus;

typedef struct {
    int32_t id;
    char ip[MAX_IP_LEN];
    int32_t nm_port;
    int32_t client_port;
    SSStatus status;
    time_t last_heartbeat;
} StorageServer;

typedef struct {
    char username[MAX_USERNAME];
    char ip[MAX_IP_LEN];
    int32_t active;
} ClientEntry;

// Thread-safety registry mutex declared in header as requested
extern pthread_mutex_t registry_mutex;

void registry_init(void);
StorageServer *registry_register_ss(const char *ip, int nm_port, int client_port);
int registry_add_file(const char *filename, StorageServer *ss);
int registry_find_file(const char *filename, StorageServer **out_servers, int32_t *out_count);
void registry_list_files(char *buffer, size_t buffer_size);

void registry_update_heartbeat(int client_port);
void registry_check_timeouts(int timeout_seconds);

int registry_register_client(const char *username, const char *ip);
int registry_is_client_registered(const char *username);

#endif