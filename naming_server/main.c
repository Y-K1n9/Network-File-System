/*
 * naming_server/main.c — Docs++ Name Server (nm_server)
 *
 * Listens on NM_PORT (5000) for connections from both Storage Servers
 * and Clients. Each connection is handled in its own thread.
 *
 * Dispatches on the first int32_t (command type):
 *
 *   CMD_REGISTER_SS        → register a Storage Server
 *   CMD_CLIENT_REGISTER    → register a Client username
 *   CMD_CLIENT_LOOKUP      → look up which SS holds a file
 *   CMD_HEARTBEAT          → SS heartbeat (on NM_HEARTBEAT_PORT 5001)
 *   CMD_CREATE  [NEW]      → create a new file (routed to SS)
 *   CMD_VIEW    [NEW]      → list files with optional flags
 *
 * Compile (as part of Makefile target nm_server):
 *   gcc -Wall -O2 -pthread \
 *       naming_server/main.c naming_server/storage_registry.c common/utils.c \
 *       -o nm_server
 */

#include "storage_registry.h"
#include "../common/protocols.h"
#include "../common/utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ─── Logging helpers ────────────────────────────────────────────────────── */
static void nm_log(const char *fmt, ...) {
    time_t now = time(NULL);
    char ts[32];
    struct tm *t = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    printf("[NM %s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ─── Per-connection argument ────────────────────────────────────────────── */
typedef struct {
    int              client_fd;
    struct sockaddr_in addr;
} Connection;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Handler: CMD_REGISTER_SS  (Y-K1n9 original)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_register_ss(int fd, int32_t cmd_type,
                                 const char *peer_ip) {
    SS_Registration_Packet packet;
    packet.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&packet) + sizeof(int32_t),
                    sizeof(packet)    - sizeof(int32_t)) != 0) {
        nm_log("Failed to receive SS_Registration_Packet from %s", peer_ip);
        return;
    }

    StorageServer *ss = registry_register_ss(packet.ip,
                                              packet.nm_port,
                                              packet.client_port);
    if (ss) {
        for (int i = 0; i < packet.file_count; ++i) {
            registry_add_file(packet.filenames[i], ss);
            /* Also add to metadata store if not already there */
            if (!registry_file_exists(packet.filenames[i])) {
                registry_create_file(packet.filenames[i], "unknown", ss->id);
            }
        }
        nm_log("Received registration from SS at %s.", packet.ip);
        nm_log("SS client port: %d, files: %d", packet.client_port,
               packet.file_count);
    } else {
        nm_log("SS registration failed for %s:%d", packet.ip,
               packet.client_port);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Handler: CMD_CLIENT_REGISTER  (Y-K1n9 original)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_client_register(int fd, int32_t cmd_type,
                                     const char *peer_ip) {
    ClientRegisterPacket packet;
    packet.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&packet) + sizeof(int32_t),
                    sizeof(packet)    - sizeof(int32_t)) != 0) {
        nm_log("Failed to receive ClientRegisterPacket from %s", peer_ip);
        return;
    }

    int status = registry_register_client(packet.username, peer_ip);
    ClientRegisterResponse resp;
    resp.status = status;
    if (status == 0) {
        snprintf(resp.message, sizeof(resp.message),
                 "Welcome, %s! Connected to Docs++.", packet.username);
        nm_log("Client registered: %s from %s", packet.username, peer_ip);
    } else {
        snprintf(resp.message, sizeof(resp.message),
                 "Registration failed (max clients reached).");
        nm_log("Client registration failed for %s", packet.username);
    }
    send_struct(fd, &resp, sizeof(resp));
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Handler: CMD_CLIENT_LOOKUP  (Y-K1n9 original)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_client_lookup(int fd, int32_t cmd_type,
                                   const char *peer_ip) {
    ClientLookupPacket packet;
    packet.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&packet) + sizeof(int32_t),
                    sizeof(packet)    - sizeof(int32_t)) != 0) {
        nm_log("Failed to receive ClientLookupPacket from %s", peer_ip);
        return;
    }

    nm_log("Lookup request: file='%s' by user='%s' from %s",
           packet.filename, packet.username, peer_ip);

    LookupResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    StorageServer *ss = registry_find_ss_for_file(packet.filename);
    if (!ss) {
        resp.status = ERR_FILE_NOT_FOUND;
        snprintf(resp.message, sizeof(resp.message),
                 "File '%s' not found.", packet.filename);
        nm_log("Lookup: file '%s' not found.", packet.filename);
    } else {
        resp.status  = ERR_OK;
        resp.ss_port = ss->client_port;
        strncpy(resp.ss_ip, ss->ip, MAX_IP_LEN - 1);
        snprintf(resp.message, sizeof(resp.message),
                 "File '%s' is on SS at %s:%d.",
                 packet.filename, ss->ip, ss->client_port);
        nm_log("Lookup OK: '%s' → %s:%d", packet.filename,
               ss->ip, ss->client_port);
    }
    send_struct(fd, &resp, sizeof(resp));
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Handler: CMD_CREATE  [NEW — Feature 1]
 *
 *  Flow:  Client → NM (CreateRequestPacket)
 *         NM → SS  (SSCreatePacket via fresh TCP connection to ss.client_port)
 *         SS → NM  (SSCreateAck)
 *         NM → Client (CreateResponsePacket)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_create(int fd, int32_t cmd_type,
                            const char *peer_ip) {
    CreateRequestPacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        nm_log("Failed to receive CreateRequestPacket from %s", peer_ip);
        return;
    }

    nm_log("CREATE request: file='%s' by user='%s' from %s",
           req.filename, req.username, peer_ip);

    CreateResponsePacket resp;
    memset(&resp, 0, sizeof(resp));

    /* 1. Check for duplicate */
    if (registry_file_exists(req.filename)) {
        resp.status = ERR_FILE_EXISTS;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: File '%s' already exists.", req.filename);
        nm_log("CREATE denied: '%s' already exists.", req.filename);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    /* 2. Pick an available SS */
    StorageServer *ss = registry_pick_ss();
    if (!ss) {
        resp.status = ERR_NO_SS_AVAILABLE;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: No Storage Server available. Try again later.");
        nm_log("CREATE denied: no SS online.");
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    /* 3. Forward CREATE to SS */
    int ss_fd = connect_to_server(ss->ip, ss->client_port);
    if (ss_fd < 0) {
        resp.status = ERR_NO_SS_AVAILABLE;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: Cannot reach Storage Server at %s:%d.",
                 ss->ip, ss->client_port);
        nm_log("CREATE: cannot connect to SS %s:%d",
               ss->ip, ss->client_port);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    SSCreatePacket ss_req;
    memset(&ss_req, 0, sizeof(ss_req));
    ss_req.command_type = CMD_SS_CREATE;
    strncpy(ss_req.filename,    req.filename, MAX_FILENAME  - 1);
    strncpy(ss_req.owner,       req.username, MAX_USERNAME  - 1);
    strncpy(ss_req.storage_dir, ".",          MAX_PATH_LEN  - 1);

    if (send_struct(ss_fd, &ss_req, sizeof(ss_req)) != 0) {
        resp.status = ERR_INTERNAL;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: Failed to send CREATE to Storage Server.");
        nm_log("CREATE: failed to send SSCreatePacket.");
        close(ss_fd);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    SSCreateAck ack;
    memset(&ack, 0, sizeof(ack));
    if (recv_struct(ss_fd, &ack, sizeof(ack)) != 0) {
        resp.status = ERR_INTERNAL;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: No acknowledgment from Storage Server.");
        nm_log("CREATE: no ACK from SS.");
        close(ss_fd);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }
    close(ss_fd);

    if (ack.status != ERR_OK) {
        resp.status = ack.status;
        strncpy(resp.message, ack.message, MAX_MESSAGE - 1);
        nm_log("CREATE: SS returned error: %s", ack.message);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    /* 4. Register in NM metadata and hash-map */
    registry_create_file(req.filename, req.username, ss->id);
    registry_add_file(req.filename, ss);
    registry_save_metadata();

    resp.status = ERR_OK;
    snprintf(resp.message, sizeof(resp.message),
             "File '%s' created successfully!", req.filename);
    nm_log("CREATE OK: '%s' by '%s' on SS[%d] %s:%d.",
           req.filename, req.username, ss->id, ss->ip, ss->client_port);

    send_struct(fd, &resp, sizeof(resp));
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Handler: CMD_VIEW  [NEW — Feature 4]
 *
 *  Flags (stored without the '-'):
 *    ""   → files the user has access to (names only)
 *    "a"  → all files on the system (names only)
 *    "l"  → user's files with details
 *    "al" → all files with details
 *
 *  Flow: Client → NM (ViewRequestPacket)
 *        NM → Client (ViewResponseHeader + N × FileInfoEntry)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_view(int fd, int32_t cmd_type,
                          const char *peer_ip) {
    ViewRequestPacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        nm_log("Failed to receive ViewRequestPacket from %s", peer_ip);
        return;
    }

    int show_all = (strchr(req.flags, 'a') != NULL);
    nm_log("VIEW request: flags='%s' user='%s' show_all=%d from %s",
           req.flags, req.username, show_all, peer_ip);

    /* Collect matching files — heap-allocated to avoid static-buffer race
     * condition since handle_view is called from multiple threads. */
    FileMeta *meta_buf = malloc(sizeof(FileMeta) * MAX_VIEW_FILES);
    if (!meta_buf) {
        ViewResponseHeader hdr_err;
        hdr_err.status     = ERR_INTERNAL;
        hdr_err.file_count = 0;
        send_struct(fd, &hdr_err, sizeof(hdr_err));
        return;
    }
    int count = registry_get_files(req.username, show_all,
                                    meta_buf, MAX_VIEW_FILES);

    /* Send header */
    ViewResponseHeader hdr;
    hdr.status     = ERR_OK;
    hdr.file_count = count;
    send_struct(fd, &hdr, sizeof(hdr));

    /* Send each FileInfoEntry */
    for (int i = 0; i < count; ++i) {
        FileInfoEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.filename, meta_buf[i].filename, MAX_FILENAME - 1);
        strncpy(entry.owner,    meta_buf[i].owner,    MAX_USERNAME  - 1);
        entry.word_count = meta_buf[i].word_count;
        entry.char_count = meta_buf[i].char_count;
        entry.size_bytes = meta_buf[i].size_bytes;
        entry.mtime      = meta_buf[i].mtime;
        entry.atime      = meta_buf[i].atime;
        entry.has_access = registry_user_has_access(
                               meta_buf[i].filename, req.username);
        send_struct(fd, &entry, sizeof(entry));
    }

    nm_log("VIEW OK: returned %d file(s) for user='%s' flags='%s'.",
           count, req.username, req.flags);
    free(meta_buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Main connection dispatcher
 * ═══════════════════════════════════════════════════════════════════════════ */
static void *handle_connection(void *arg) {
    Connection *conn = (Connection *)arg;
    int fd           = conn->client_fd;
    char peer_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &conn->addr.sin_addr, peer_ip, sizeof(peer_ip));
    free(conn);

    int32_t cmd_type = 0;
    if (recv_all(fd, &cmd_type, sizeof(cmd_type)) < 0) {
        close(fd);
        return NULL;
    }

    switch ((CommandType)cmd_type) {
        case CMD_REGISTER_SS:
            handle_register_ss(fd, cmd_type, peer_ip);
            break;
        case CMD_CLIENT_REGISTER:
            handle_client_register(fd, cmd_type, peer_ip);
            break;
        case CMD_CLIENT_LOOKUP:
            handle_client_lookup(fd, cmd_type, peer_ip);
            break;
        case CMD_CREATE:
            handle_create(fd, cmd_type, peer_ip);
            break;
        case CMD_VIEW:
            handle_view(fd, cmd_type, peer_ip);
            break;
        default:
            nm_log("Unknown command %d from %s — ignored.", cmd_type, peer_ip);
            break;
    }

    close(fd);
    return NULL;
}

/* ─── Heartbeat listener (port 5001) ─────────────────────────────────────── */
static void *heartbeat_listener(void *arg) {
    (void)arg;
    int hb_sock = create_server_socket(NM_HEARTBEAT_PORT);
    if (hb_sock < 0) {
        nm_log("Failed to bind heartbeat port %d.", NM_HEARTBEAT_PORT);
        return NULL;
    }
    nm_log("[NM Heartbeat] Listening for heartbeats on port %d.",
           NM_HEARTBEAT_PORT);

    while (1) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int cfd = accept(hb_sock, (struct sockaddr *)&addr, &len);
        if (cfd < 0) continue;

        HeartbeatPacket hb;
        if (recv_struct(cfd, &hb, sizeof(hb)) == 0) {
            registry_update_heartbeat(hb.ip, hb.port);
        }
        close(cfd);
    }
    return NULL;
}

/* ─── Main ───────────────────────────────────────────────────────────────── */
int main(void) {
    registry_init();
    registry_load_metadata();

    int server_sock = create_server_socket(NM_PORT);
    if (server_sock < 0) {
        fprintf(stderr, "[NM] Failed to bind port %d\n", NM_PORT);
        return 1;
    }

    /* Start heartbeat listener in background */
    pthread_t hb_tid;
    pthread_create(&hb_tid, NULL, heartbeat_listener, NULL);
    pthread_detach(hb_tid);

    nm_log("=== Docs++ Name Server started on port %d ===", NM_PORT);
    nm_log("Waiting for Storage Servers and Clients...");

    while (1) {
        Connection *conn = malloc(sizeof(Connection));
        if (!conn) continue;
        socklen_t addrlen = sizeof(conn->addr);
        conn->client_fd = accept(server_sock,
                                  (struct sockaddr *)&conn->addr, &addrlen);
        if (conn->client_fd < 0) { free(conn); continue; }

        pthread_t tid;
        pthread_create(&tid, NULL, handle_connection, conn);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
