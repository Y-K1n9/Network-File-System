/*
 * naming_server/storage_registry.c
 *
 * In-memory registry for the Name Server.
 * Tracks: Storage Servers, file→SS mapping (hash map), clients, file metadata.
 *
 * ── Preserved from Y-K1n9 ────────────────────────────────────────────────
 *   Hash-map with djb2 hash, REGISTRY_BUCKETS=256, FileMapNode chain.
 *   registry_register_ss, registry_add_file, registry_register_client,
 *   registry_find_ss_for_file, registry_pick_ss, registry_update_heartbeat.
 *
 * ── New additions ─────────────────────────────────────────────────────────
 *   FileMeta array, registry_create_file, registry_get_files,
 *   registry_user_has_access, registry_save_metadata, registry_load_metadata.
 */

#include "storage_registry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Internal hash-map node (file → SS mapping) ─────────────────────────── */
typedef struct FileMapNode {
    char           filename[MAX_FILENAME];
    StorageServer *servers[MAX_REPLICAS];
    int32_t        server_count;
    struct FileMapNode *next;
} FileMapNode;

/* ─── Global state ───────────────────────────────────────────────────────── */
static FileMapNode  *buckets[REGISTRY_BUCKETS];
static StorageServer active_servers[MAX_STORAGE_SERVERS];
static int32_t       active_server_count = 0;

static ClientEntry   active_clients[MAX_CLIENTS];
static int32_t       active_client_count = 0;

/* File metadata array (new, for CREATE/VIEW) */
static FileMeta      g_files[MAX_NM_FILES];
static int32_t       g_file_count = 0;

pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── djb2 hash (Y-K1n9 original) ───────────────────────────────────────── */
static unsigned long hash_file(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0)
        h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Y-K1n9 original functions
 * ═══════════════════════════════════════════════════════════════════════════ */

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

    /* New SS */
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

    /* Check for existing entry and add replica */
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

    /* New entry */
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
        return -1;
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

/* ═══════════════════════════════════════════════════════════════════════════
 *  New additions — Feature 1 (CREATE) and Feature 4 (VIEW)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Internal helper: find file meta by name (caller must hold mutex) */
static FileMeta *find_meta(const char *filename) {
    for (int i = 0; i < g_file_count; ++i) {
        if (g_files[i].active &&
            strcmp(g_files[i].filename, filename) == 0)
            return &g_files[i];
    }
    return NULL;
}

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
        return -1;   /* already exists */
    }
    if (g_file_count >= MAX_NM_FILES) {
        pthread_mutex_unlock(&registry_mutex);
        return -1;   /* store full */
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
    return 0;
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
    }
    pthread_mutex_unlock(&registry_mutex);
}

/* ─── Persistence ────────────────────────────────────────────────────────── */
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
