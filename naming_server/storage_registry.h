/*
 * naming_server/storage_registry.h
 *
 * Declares the in-memory registry that the NM uses to track:
 *   - Storage Servers (SS) and which files each holds
 *   - Connected clients
 *   - File metadata (owner, ACL, timestamps, word/char counts)
 *
 * ── Y-K1n9 original functions ────────────────────────────────────────────
 *   registry_init, registry_register_ss, registry_add_file,
 *   registry_register_client, registry_find_ss_for_file,
 *   registry_pick_ss, registry_update_heartbeat
 *
 * ── New additions for CREATE and VIEW ────────────────────────────────────
 *   registry_create_file, registry_file_exists, registry_get_files,
 *   registry_user_has_access, registry_save_metadata, registry_load_metadata
 */

#ifndef STORAGE_REGISTRY_H
#define STORAGE_REGISTRY_H

#include "../common/protocols.h"
#include <pthread.h>
#include <stddef.h>
#include <time.h>

/* ─── Registry Limits ────────────────────────────────────────────────────── */
#define MAX_STORAGE_SERVERS  16
#define MAX_REPLICAS          3
#define MAX_CLIENTS          32
#define MAX_ACL_ENTRIES      32
#define MAX_NM_FILES        512   /* max files NM tracks */
#define REGISTRY_BUCKETS    256   /* hash-map buckets for file→SS lookup */

/* ─── Storage Server Status ──────────────────────────────────────────────── */
typedef enum {
    SS_STATUS_OFFLINE = 0,
    SS_STATUS_ONLINE  = 1
} SSStatus;

/* ─── Storage Server Record ──────────────────────────────────────────────── */
typedef struct {
    int32_t  id;
    char     ip[MAX_IP_LEN];
    int32_t  nm_port;
    int32_t  client_port;
    SSStatus status;
    time_t   last_heartbeat;
} StorageServer;

/* ─── Connected Client Record ────────────────────────────────────────────── */
typedef struct {
    char    username[MAX_USERNAME];
    char    ip[MAX_IP_LEN];
    int32_t status;   /* 0 = inactive, 1 = active */
} ClientEntry;

/* ─── Per-file Access Control Entry ─────────────────────────────────────── */
typedef struct {
    char    username[MAX_USERNAME];
    int32_t level;   /* 0 = none, 1 = read, 2 = read+write */
} ACLEntry;

/* ─── File Metadata (NM-side) ────────────────────────────────────────────── */
typedef struct {
    char     filename[MAX_FILENAME];
    char     owner[MAX_USERNAME];
    int32_t  ss_id;        /* index into active_servers[] */
    int32_t  word_count;
    int32_t  char_count;
    int64_t  size_bytes;
    int64_t  created;      /* unix timestamps */
    int64_t  mtime;
    int64_t  atime;
    ACLEntry acl[MAX_ACL_ENTRIES];
    int32_t  acl_count;
    int32_t  active;       /* 1 = exists, 0 = deleted */
} FileMeta;

/* Shared mutex (used by registry and heartbeat threads) */
extern pthread_mutex_t registry_mutex;

/* ═══════════════════════════════════════════════════════════════════════════
 * Original Y-K1n9 API
 * ═══════════════════════════════════════════════════════════════════════════ */
void           registry_init(void);
StorageServer *registry_register_ss(const char *ip, int nm_port, int client_port);
void           registry_add_file(const char *filename, StorageServer *ss);
int            registry_register_client(const char *username, const char *ip);
StorageServer *registry_find_ss_for_file(const char *filename);
StorageServer *registry_pick_ss(void);
void           registry_update_heartbeat(const char *ip, int port);

/* ═══════════════════════════════════════════════════════════════════════════
 * New API — Feature 1 (CREATE) and Feature 4 (VIEW)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Returns 1 if the file is already tracked by the NM, 0 otherwise */
int       registry_file_exists(const char *filename);

/* Record a newly-created file in the NM metadata store.
 * Returns 0 on success, -1 if file already exists or store is full. */
int       registry_create_file(const char *filename, const char *owner, int ss_id);

/* Returns the access level (0=none,1=read,2=write) for <username> on <filename> */
int       registry_user_has_access(const char *filename, const char *username);

/* Populate <out> with up to <max_count> FileMeta entries.
 * If show_all=0 only include files <username> has access to.
 * Returns number of entries written. */
int       registry_get_files(const char *username, int show_all,
                              FileMeta *out, int max_count);

/* Update cached word/char/size stats for a file */
void      registry_update_file_stats(const char *filename,
                                      int words, int chars, int64_t size);

/* Persist / restore NM file metadata to/from "nm_files.json" */
void      registry_save_metadata(void);
void      registry_load_metadata(void);

#endif /* STORAGE_REGISTRY_H */
