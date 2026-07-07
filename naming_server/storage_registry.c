/*
 * naming_server/storage_registry.c
 *
 * In-memory registry for the Name Server.
 * Tracks: Storage Servers, file→SS mapping (hash map), clients, file metadata.
 */

#include "storage_registry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- Internal hash-map node (file → SS mapping) --------------------------- */
typedef struct FileMapNode {
    char           filename[MAX_FILENAME];
    StorageServer *servers[MAX_REPLICAS];
    int32_t        server_count;
    struct FileMapNode *next;
} FileMapNode;

/* --- Global state --------------------------------------------------------- */
static FileMapNode  *buckets[REGISTRY_BUCKETS];
static StorageServer active_servers[MAX_STORAGE_SERVERS];
static int32_t       active_server_count = 0;

static ClientEntry   active_clients[MAX_CLIENTS];
static int32_t       active_client_count = 0;

static FileMeta      g_files[MAX_NM_FILES];
static int32_t       g_file_count = 0;

pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --- djb2 hash ------------------------------------------------------------ */
static unsigned long hash_file(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0)
        h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

/* --- Internal: find file meta by name (caller must hold mutex) ------------ */
static FileMeta *find_meta(const char *filename) {
    for (int i = 0; i < g_file_count; ++i) {
        if (g_files[i].active &&
            strcmp(g_files[i].filename, filename) == 0)
            return &g_files[i];
    }
    return NULL;
}

/* ==========================================================================
 *  Core registry functions
 * ========================================================================== */

void registry_init(void) {
    pthread_mutex_lock(&registry_mutex);
    active_server_count = 0;
    active_client_count = 0;
    g_file_count        = 0;
    memset(active_servers, 0, sizeof(active_servers));
    memset(active_clients, 0, sizeof(active_clients));
    memset(g_files,        0, sizeof(g_files));
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

    /* Re-registration: same IP + nm_port → bring back online */
    for (int i = 0; i < active_server_count; ++i) {
        if (strcmp(active_servers[i].ip, ip) == 0 &&
            active_servers[i].nm_port == nm_port) {
            active_servers[i].client_port    = client_port;
            active_servers[i].status         = SS_STATUS_ONLINE;
            active_servers[i].last_heartbeat = time(NULL);
            StorageServer *ss = &active_servers[i];
            pthread_mutex_unlock(&registry_mutex);
            return ss;
        }
    }

    if (active_server_count >= MAX_STORAGE_SERVERS) {
        pthread_mutex_unlock(&registry_mutex);
        return NULL;
    }
    StorageServer *ss = &active_servers[active_server_count];
    ss->id             = active_server_count;
    ss->nm_port        = nm_port;
    ss->client_port    = client_port;
    ss->status         = SS_STATUS_ONLINE;
    ss->last_heartbeat = time(NULL);
    strncpy(ss->ip, ip, MAX_IP_LEN - 1);
    active_server_count++;
    pthread_mutex_unlock(&registry_mutex);
    return ss;
}

void registry_add_file(const char *filename, StorageServer *ss) {
    if (!filename || !ss) return;
    unsigned long h = hash_file(filename) % REGISTRY_BUCKETS;
    pthread_mutex_lock(&registry_mutex);

    FileMapNode *node = buckets[h];
    while (node) {
        if (strcmp(node->filename, filename) == 0) {
            if (node->server_count < MAX_REPLICAS)
                node->servers[node->server_count++] = ss;
            pthread_mutex_unlock(&registry_mutex);
            return;
        }
        node = node->next;
    }

    FileMapNode *new_node = malloc(sizeof(FileMapNode));
    if (!new_node) { pthread_mutex_unlock(&registry_mutex); return; }
    memset(new_node, 0, sizeof(*new_node));
    strncpy(new_node->filename, filename, MAX_FILENAME - 1);
    new_node->servers[0]   = ss;
    new_node->server_count = 1;
    new_node->next         = buckets[h];
    buckets[h]             = new_node;
    pthread_mutex_unlock(&registry_mutex);
}

int registry_register_client(const char *username, const char *ip) {
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < active_client_count; ++i) {
        if (strcmp(active_clients[i].username, username) == 0) {
            strncpy(active_clients[i].ip, ip, MAX_IP_LEN - 1);
            active_clients[i].status = 1;
            pthread_mutex_unlock(&registry_mutex);
            return 0;
        }
    }
    if (active_client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_MAX_CLIENTS;
    }
    ClientEntry *c = &active_clients[active_client_count++];
    strncpy(c->username, username, MAX_USERNAME - 1);
    strncpy(c->ip, ip, MAX_IP_LEN - 1);
    c->status = 1;
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

StorageServer *registry_find_ss_for_file(const char *filename) {
    if (!filename) return NULL;
    unsigned long h = hash_file(filename) % REGISTRY_BUCKETS;
    pthread_mutex_lock(&registry_mutex);
    FileMapNode *node = buckets[h];
    while (node) {
        if (strcmp(node->filename, filename) == 0) {
            for (int i = 0; i < node->server_count; ++i) {
                if (node->servers[i] &&
                    node->servers[i]->status == SS_STATUS_ONLINE) {
                    StorageServer *ss = node->servers[i];
                    pthread_mutex_unlock(&registry_mutex);
                    return ss;
                }
            }
        }
        node = node->next;
    }
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

StorageServer *registry_pick_ss(void) {
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < active_server_count; ++i) {
        if (active_servers[i].status == SS_STATUS_ONLINE) {
            StorageServer *ss = &active_servers[i];
            pthread_mutex_unlock(&registry_mutex);
            return ss;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

void registry_update_heartbeat(const char *ip, int port) {
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < active_server_count; ++i) {
        if (strcmp(active_servers[i].ip, ip) == 0 &&
            active_servers[i].client_port == port) {
            active_servers[i].last_heartbeat = time(NULL);
            active_servers[i].status         = SS_STATUS_ONLINE;
            break;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
}

/* ==========================================================================
 *  File metadata functions
 * ========================================================================== */

int registry_file_exists(const char *filename) {
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    pthread_mutex_unlock(&registry_mutex);
    return m != NULL;
}

int registry_create_file(const char *filename, const char *owner, int ss_id) {
    pthread_mutex_lock(&registry_mutex);
    if (find_meta(filename)) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_FILE_EXISTS;
    }
    if (g_file_count >= MAX_NM_FILES) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_MAX_FILES;
    }
    FileMeta *m = &g_files[g_file_count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->filename, filename, MAX_FILENAME - 1);
    strncpy(m->owner,    owner,    MAX_USERNAME  - 1);
    m->ss_id   = ss_id;
    m->active  = 1;
    m->created = m->mtime = m->atime = (int64_t)time(NULL);
    /* Owner gets read+write */
    strncpy(m->acl[0].username, owner, MAX_USERNAME - 1);
    m->acl[0].level = 2;
    m->acl_count    = 1;
    pthread_mutex_unlock(&registry_mutex);
    return ERR_OK;
}

int registry_user_has_access(const char *filename, const char *username) {
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    if (!m) { pthread_mutex_unlock(&registry_mutex); return 0; }
    for (int i = 0; i < m->acl_count; ++i) {
        if (strcmp(m->acl[i].username, username) == 0) {
            int level = m->acl[i].level;
            pthread_mutex_unlock(&registry_mutex);
            return level;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

int registry_grant_access(const char *filename, const char *requester, const char *target_user, int level) {
    if (!filename || !requester || !target_user) return ERR_INTERNAL;
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    if (!m) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_FILE_NOT_FOUND;
    }
    if (strcmp(m->owner, requester) != 0) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_NO_PERMISSION;
    }

    int found = 0;
    for (int i = 0; i < m->acl_count; ++i) {
        if (strcmp(m->acl[i].username, target_user) == 0) {
            m->acl[i].level = level;
            found = 1;
            break;
        }
    }

    if (!found) {
        if (m->acl_count >= MAX_ACL_ENTRIES) {
            pthread_mutex_unlock(&registry_mutex);
            return ERR_INTERNAL;
        }
        strncpy(m->acl[m->acl_count].username, target_user, MAX_USERNAME - 1);
        m->acl[m->acl_count].level = level;
        m->acl_count++;
    }

    pthread_mutex_unlock(&registry_mutex);
    registry_save_metadata();
    return ERR_OK;
}

int registry_remove_access(const char *filename, const char *requester, const char *target_user) {
    if (!filename || !requester || !target_user) return ERR_INTERNAL;
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    if (!m) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_FILE_NOT_FOUND;
    }
    if (strcmp(m->owner, requester) != 0) {
        pthread_mutex_unlock(&registry_mutex);
        return ERR_NO_PERMISSION;
    }
    if (strcmp(m->owner, target_user) == 0) {
        /* Cannot remove owner access */
        pthread_mutex_unlock(&registry_mutex);
        return ERR_NO_PERMISSION;
    }

    int found_idx = -1;
    for (int i = 0; i < m->acl_count; ++i) {
        if (strcmp(m->acl[i].username, target_user) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx >= 0) {
        for (int i = found_idx; i < m->acl_count - 1; ++i) {
            m->acl[i] = m->acl[i + 1];
        }
        m->acl_count--;
    }

    pthread_mutex_unlock(&registry_mutex);
    registry_save_metadata();
    return ERR_OK;
}

int registry_get_file_info(const char *filename, FileMeta *out) {
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    if (!m) {
        pthread_mutex_unlock(&registry_mutex);
        return -1;
    }
    *out = *m;
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

int registry_get_files(const char *username, int show_all,
                        FileMeta *out, int max_count) {
    pthread_mutex_lock(&registry_mutex);
    int count = 0;
    for (int i = 0; i < g_file_count && count < max_count; ++i) {
        FileMeta *m = &g_files[i];
        if (!m->active) continue;
        if (!show_all) {
            int has_access = 0;
            for (int j = 0; j < m->acl_count; ++j) {
                if (strcmp(m->acl[j].username, username) == 0) {
                    has_access = 1;
                    break;
                }
            }
            if (!has_access) continue;
        }
        out[count++] = *m;
    }
    pthread_mutex_unlock(&registry_mutex);
    return count;
}

void registry_update_file_stats(const char *filename,
                                  int words, int chars, int64_t size) {
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    if (m) {
        m->word_count = words;
        m->char_count = chars;
        m->size_bytes = size;
        m->atime      = (int64_t)time(NULL);
        m->mtime      = (int64_t)time(NULL);
    }
    pthread_mutex_unlock(&registry_mutex);
}

const char *registry_get_file_owner(const char *filename) {
    pthread_mutex_lock(&registry_mutex);
    FileMeta *m = find_meta(filename);
    const char *owner = m ? m->owner : NULL;
    pthread_mutex_unlock(&registry_mutex);
    return owner;
}

int registry_delete_file(const char *filename, StorageServer *out_servers[], int *out_server_count) {
    if (!filename) return ERR_FILE_NOT_FOUND;
    unsigned long h = hash_file(filename) % REGISTRY_BUCKETS;
    pthread_mutex_lock(&registry_mutex);

    /* Remove from hash-map */
    FileMapNode *prev = NULL;
    FileMapNode *curr = buckets[h];
    int found_in_map = 0;
    *out_server_count = 0;

    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            found_in_map = 1;
            *out_server_count = curr->server_count;
            for (int i = 0; i < curr->server_count; ++i) {
                out_servers[i] = curr->servers[i];
            }
            if (prev) {
                prev->next = curr->next;
            } else {
                buckets[h] = curr->next;
            }
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    /* Deactivate in metadata */
    FileMeta *m = find_meta(filename);
    if (m) {
        m->active = 0;
    }

    pthread_mutex_unlock(&registry_mutex);

    if (found_in_map || m) {
        registry_save_metadata();
        return ERR_OK;
    }
    return ERR_FILE_NOT_FOUND;
}

/* ==========================================================================
 *  Client query
 * ========================================================================== */

int registry_get_clients(ClientEntry *out, int max_count) {
    pthread_mutex_lock(&registry_mutex);
    int count = 0;
    for (int i = 0; i < active_client_count && count < max_count; ++i) {
        out[count++] = active_clients[i];
    }
    pthread_mutex_unlock(&registry_mutex);
    return count;
}

/* ==========================================================================
 *  Persistence
 * ========================================================================== */

void registry_save_metadata(void) {
    FILE *f = fopen("nm_files.json", "w");
    if (!f) return;
    fprintf(f, "[\n");
    pthread_mutex_lock(&registry_mutex);
    int first = 1;
    for (int i = 0; i < g_file_count; ++i) {
        FileMeta *m = &g_files[i];
        if (!m->active) continue;
        if (!first) fprintf(f, ",\n");
        first = 0;
        fprintf(f,
            "  {\"name\":\"%s\",\"owner\":\"%s\",\"ss_id\":%d,"
            "\"words\":%d,\"chars\":%d,\"size\":%lld,"
            "\"created\":%lld,\"mtime\":%lld,\"atime\":%lld}",
            m->filename, m->owner, m->ss_id,
            m->word_count, m->char_count,
            (long long)m->size_bytes,
            (long long)m->created,
            (long long)m->mtime,
            (long long)m->atime);
    }
    pthread_mutex_unlock(&registry_mutex);
    fprintf(f, "\n]\n");
    fclose(f);
}

void registry_load_metadata(void) {
    FILE *f = fopen("nm_files.json", "r");
    if (!f) return;

    char line[2048];
    pthread_mutex_lock(&registry_mutex);
    g_file_count = 0;
    while (fgets(line, sizeof(line), f) && g_file_count < MAX_NM_FILES) {
        if (!strstr(line, "\"name\"")) continue;
        FileMeta *m = &g_files[g_file_count];
        memset(m, 0, sizeof(*m));
        char name[MAX_FILENAME], owner[MAX_USERNAME];
        int  ss_id, words, chars;
        long long size, created, mtime, atime;
        if (sscanf(line,
            "  {\"name\":\"%255[^\"]\",\"owner\":\"%63[^\"]\"," \
            "\"ss_id\":%d,\"words\":%d,\"chars\":%d,\"size\":%lld," \
            "\"created\":%lld,\"mtime\":%lld,\"atime\":%lld}",
            name, owner, &ss_id, &words, &chars,
            &size, &created, &mtime, &atime) == 9) {
            strncpy(m->filename, name,  MAX_FILENAME - 1);
            strncpy(m->owner,    owner, MAX_USERNAME  - 1);
            m->ss_id      = ss_id;
            m->word_count = words;
            m->char_count = chars;
            m->size_bytes = (int64_t)size;
            m->created    = (int64_t)created;
            m->mtime      = (int64_t)mtime;
            m->atime      = (int64_t)atime;
            m->active     = 1;
            /* Restore owner ACL */
            strncpy(m->acl[0].username, owner, MAX_USERNAME - 1);
            m->acl[0].level = 2;
            m->acl_count    = 1;
            g_file_count++;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    fclose(f);
    printf("[NM Registry] Loaded %d file(s) from persistent metadata.\n",
           g_file_count);
}
