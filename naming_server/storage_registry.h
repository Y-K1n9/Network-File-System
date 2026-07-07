/*
 * naming_server/storage_registry.h
 *
 * In-memory registry for the NM. Tracks:
 *   - Storage Servers (SS) and file→SS mapping
 *   - Connected clients
 *   - File metadata (owner, ACL, timestamps, stats)
 */

#ifndef STORAGE_REGISTRY_H
#define STORAGE_REGISTRY_H

#include "../common/protocols.h"
#include <pthread.h>
#include <stddef.h>
#include <time.h>

/* --- Registry Limits ------------------------------------------------------ */
#define MAX_STORAGE_SERVERS  16
#define MAX_REPLICAS          3
#define MAX_CLIENTS          32
#define MAX_ACL_ENTRIES      32
#define MAX_NM_FILES        512
#define REGISTRY_BUCKETS    256

/* --- Storage Server Status ------------------------------------------------ */
typedef enum {
    SS_STATUS_OFFLINE = 0,
    SS_STATUS_ONLINE  = 1
} SSStatus;

/* --- Storage Server Record ------------------------------------------------ */
typedef struct {
    int32_t  id;
    char     ip[MAX_IP_LEN];
    int32_t  nm_port;
    int32_t  client_port;
    SSStatus status;
    time_t   last_heartbeat;
} StorageServer;

/* --- Connected Client Record ---------------------------------------------- */
typedef struct {
    char    username[MAX_USERNAME];
    char    ip[MAX_IP_LEN];
    int32_t status;   /* 0 = inactive, 1 = active */
} ClientEntry;

/* --- Per-file Access Control Entry ---------------------------------------- */
typedef struct {
    char    username[MAX_USERNAME];
    int32_t level;   /* 0 = none, 1 = read, 2 = read+write */
} ACLEntry;

/* --- File Metadata (NM-side) ---------------------------------------------- */
typedef struct {
    char     filename[MAX_FILENAME];
    char     owner[MAX_USERNAME];
    int32_t  ss_id;
    int32_t  word_count;
    int32_t  char_count;
    int64_t  size_bytes;
    int64_t  created;
    int64_t  mtime;
    int64_t  atime;
    ACLEntry acl[MAX_ACL_ENTRIES];
    int32_t  acl_count;
    int32_t  active;       /* 1 = exists, 0 = deleted */
} FileMeta;

/* Shared mutex */
extern pthread_mutex_t registry_mutex;

/* --- Core API ------------------------------------------------------------- */
void           registry_init(void);
StorageServer *registry_register_ss(const char *ip, int nm_port, int client_port);
void           registry_add_file(const char *filename, StorageServer *ss);
int            registry_register_client(const char *username, const char *ip);
StorageServer *registry_find_ss_for_file(const char *filename);
StorageServer *registry_pick_ss(void);
void           registry_update_heartbeat(const char *ip, int port);

/* --- File metadata API ---------------------------------------------------- */
int            registry_file_exists(const char *filename);
int            registry_create_file(const char *filename, const char *owner, int ss_id);
/* Returns the access level (0=none,1=read,2=write) for <username> on <filename> */
int            registry_user_has_access(const char *filename, const char *username);
int            registry_grant_access(const char *filename, const char *requester, const char *target_user, int level);
int            registry_remove_access(const char *filename, const char *requester, const char *target_user);

/* Retrieves the full FileMeta record for a file. Returns 0 if found, -1 if not. */
int            registry_get_file_info(const char *filename, FileMeta *out);

/* Populate <out> with up to <max_count> FileMeta entries. */
int            registry_get_files(const char *username, int show_all,
                                   FileMeta *out, int max_count);
void           registry_update_file_stats(const char *filename,
                                           int words, int chars, int64_t size);
const char    *registry_get_file_owner(const char *filename);
int            registry_delete_file(const char *filename,
                                     StorageServer *out_servers[], int *out_server_count);

/* --- Client query API ----------------------------------------------------- */
int            registry_get_clients(ClientEntry *out, int max_count);

/* --- Persistence ---------------------------------------------------------- */
void           registry_save_metadata(void);
void           registry_load_metadata(void);

#endif /* STORAGE_REGISTRY_H */
