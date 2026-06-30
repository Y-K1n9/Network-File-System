/*
 * storage_server/main.c — Docs++ Storage Server (ss_server)
 *
 * Usage:
 *   ./ss_server <nm_ip> <nm_port> <ss_client_port> <storage_dir>
 *   e.g.: ./ss_server 127.0.0.1 5000 6000 ./ss_storage
 *
 * On startup:
 *   1. Scans <storage_dir> for existing files.
 *   2. Connects to NM and sends SS_Registration_Packet.
 *   3. Starts a background thread that sends heartbeats to NM_HEARTBEAT_PORT
 *      every 1.5 seconds.
 *   4. Listens on <ss_client_port> for incoming connections (from both NM and
 *      direct client ops).
 *
 * Dispatches on the first int32_t (command type):
 *   CMD_FILE_READ   → stream file content to client  (Y-K1n9 original)
 *   CMD_SS_CREATE   → create a new empty file        [NEW — Feature 1]
 *
 * Compile (as part of Makefile target ss_server):
 *   gcc -Wall -O2 -pthread \
 *       storage_server/main.c storage_server/file_handler.c common/utils.c \
 *       -o ss_server
 */

#include "file_handler.h"
#include "../common/protocols.h"
#include "../common/utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

/* ─── Globals set from argv ──────────────────────────────────────────────── */
static char  g_nm_ip[MAX_IP_LEN]       = "127.0.0.1";
static int   g_nm_port                 = NM_PORT;
static int   g_client_port             = SS_DEFAULT_PORT;
static char  g_storage_dir[MAX_PATH_LEN] = "./ss_storage";

/* ─── Logging ────────────────────────────────────────────────────────────── */
static void ss_log(const char *fmt, ...) {
    time_t now = time(NULL);
    char ts[32];
    struct tm *t = localtime(&now);
    strftime(ts, sizeof(ts), "%H:%M:%S", t);
    printf("[SS %s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ─── Security: block path traversal ─────────────────────────────────────── */
static int contains_path_traversal(const char *path) {
    if (!path) return 0;
    if (path[0] == '/') return 1;
    if (strstr(path, "../") != NULL) return 1;
    if (strstr(path, "/..") != NULL) return 1;
    if (strcmp(path, "..") == 0) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  CMD_FILE_READ handler  (Y-K1n9 original)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_file_read(int fd, const char *filename) {
    if (contains_path_traversal(filename)) {
        ss_log("Blocked path-traversal attempt: '%s'", filename);
        ReadResponseHeader hdr = {
            .command_type   = CMD_FILE_READ,
            .status         = -1,
            .content_length = 0
        };
        send_struct(fd, &hdr, sizeof(hdr));
        return;
    }

    char *buffer = NULL;
    long  length = 0;
    if (read_file_into_buffer(g_storage_dir, filename, &buffer, &length) < 0) {
        ReadResponseHeader hdr = {
            .command_type   = CMD_FILE_READ,
            .status         = ERR_FILE_NOT_FOUND,
            .content_length = 0
        };
        send_struct(fd, &hdr, sizeof(hdr));
        ss_log("READ '%s' — file not found.", filename);
        return;
    }

    ReadResponseHeader hdr = {
        .command_type   = CMD_FILE_READ,
        .status         = ERR_OK,
        .content_length = length
    };
    send_struct(fd, &hdr, sizeof(hdr));
    if (length > 0) send_all(fd, buffer, (size_t)length);
    free(buffer);
    ss_log("READ '%s' — sent %ld bytes.", filename, length);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  CMD_SS_CREATE handler  [NEW — Feature 1]
 *
 *  NM sends SSCreatePacket (already has cmd_type consumed).
 *  SS creates empty file, sends SSCreateAck back to NM.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void handle_ss_create(int fd, int32_t cmd_type) {
    SSCreatePacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        ss_log("Failed to receive SSCreatePacket.");
        return;
    }

    ss_log("CREATE request: file='%s' owner='%s'", req.filename, req.owner);

    SSCreateAck ack;
    memset(&ack, 0, sizeof(ack));

    /* Security check */
    if (contains_path_traversal(req.filename)) {
        ack.status = ERR_INTERNAL;
        snprintf(ack.message, sizeof(ack.message),
                 "ERROR: Invalid filename '%s'.", req.filename);
        ss_log("CREATE blocked: path traversal in '%s'.", req.filename);
        send_struct(fd, &ack, sizeof(ack));
        return;
    }

    int rc = create_empty_file(g_storage_dir, req.filename);
    if (rc < 0) {
        ack.status = ERR_FILE_EXISTS;
        snprintf(ack.message, sizeof(ack.message),
                 "ERROR: File '%s' already exists on disk.", req.filename);
        ss_log("CREATE failed: '%s' already on disk.", req.filename);
    } else {
        ack.status = ERR_OK;
        snprintf(ack.message, sizeof(ack.message),
                 "File '%s' created on SS.", req.filename);
        ss_log("CREATE OK: '%s'.", req.filename);
    }
    send_struct(fd, &ack, sizeof(ack));
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Per-connection thread
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    int  client_fd;
    char storage_dir[MAX_PATH_LEN];
} ClientConnection;

static void *handle_client_connection(void *arg) {
    ClientConnection *cc = (ClientConnection *)arg;
    int fd = cc->client_fd;
    free(cc);

    int32_t cmd_type = 0;
    if (recv_all(fd, &cmd_type, sizeof(cmd_type)) < 0) {
        close(fd);
        return NULL;
    }

    if (cmd_type == CMD_FILE_READ) {
        /* Read the filename (client sends it after the cmd_type) */
        char filename[MAX_FILENAME] = {0};
        if (recv_all(fd, filename, sizeof(filename)) < 0) {
            close(fd);
            return NULL;
        }
        handle_file_read(fd, filename);

    } else if (cmd_type == CMD_SS_CREATE) {
        handle_ss_create(fd, cmd_type);

    } else {
        ss_log("Unknown command %d — ignored.", cmd_type);
    }

    close(fd);
    return NULL;
}

/* ─── Heartbeat sender ───────────────────────────────────────────────────── */
typedef struct {
    char nm_ip[MAX_IP_LEN];
    int  nm_heartbeat_port;
    int  client_port;
} HeartbeatPingArgs;

static void *heartbeat_sender(void *arg) {
    HeartbeatPingArgs *hpa = (HeartbeatPingArgs *)arg;
    HeartbeatPacket pkt;
    pkt.command_type = CMD_HEARTBEAT;
    pkt.port         = hpa->client_port;
    strncpy(pkt.ip, g_nm_ip, MAX_IP_LEN - 1);  /* SS's own IP */

    while (1) {
        usleep(1500000);  /* 1.5 seconds */
        int hb_fd = connect_to_server(hpa->nm_ip, hpa->nm_heartbeat_port);
        if (hb_fd >= 0) {
            send_struct(hb_fd, &pkt, sizeof(pkt));
            close(hb_fd);
        }
    }
    free(hpa);
    return NULL;
}

/* ─── Register with NM ───────────────────────────────────────────────────── */
static int register_with_nm(void) {
    SS_Registration_Packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.command_type = CMD_REGISTER_SS;
    packet.nm_port      = g_client_port;  /* port NM can reach this SS on */
    packet.client_port  = g_client_port;

    /* Scan storage directory */
    if (scan_storage_directory(g_storage_dir, &packet) < 0) {
        ss_log("Warning: could not scan '%s' — registering with 0 files.",
               g_storage_dir);
        packet.file_count = 0;
    }

    /* Connect for registration */
    int nm_fd = connect_to_server(g_nm_ip, g_nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "[SS] Cannot connect to NM for registration.\n");
        return -1;
    }
    
    /* Determine this machine's IP (use NM connection source address) */
    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    getsockname(nm_fd, (struct sockaddr *)&local, &len);
    inet_ntop(AF_INET, &local.sin_addr, packet.ip, MAX_IP_LEN);

    if (send_struct(nm_fd, &packet, sizeof(packet)) != 0) {
        fprintf(stderr, "[SS] Failed to send registration packet.\n");
        close(nm_fd);
        return -1;
    }
    close(nm_fd);

    ss_log("Registered %d files with NM at %s:%d",
           packet.file_count, g_nm_ip, g_nm_port);
    return 0;
}

/* ─── Main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc > 1) strncpy(g_nm_ip,       argv[1], MAX_IP_LEN - 1);
    if (argc > 2) g_nm_port    = atoi(argv[2]);
    if (argc > 3) g_client_port = atoi(argv[3]);
    if (argc > 4) strncpy(g_storage_dir, argv[4], MAX_PATH_LEN - 1);

    /* Ensure storage directory exists */
    struct stat st;
    if (stat(g_storage_dir, &st) != 0) {
        mkdir(g_storage_dir, 0755);
        ss_log("Created storage directory: %s", g_storage_dir);
    }

    if (register_with_nm() < 0) {
        fprintf(stderr, "[SS] Registration failed. Exiting.\n");
        return 1;
    }

    /* Start heartbeat thread */
    HeartbeatPingArgs *hpa = malloc(sizeof(HeartbeatPingArgs));
    strncpy(hpa->nm_ip, g_nm_ip, MAX_IP_LEN - 1);
    hpa->nm_heartbeat_port = NM_HEARTBEAT_PORT;
    hpa->client_port       = g_client_port;
    pthread_t hb_tid;
    pthread_create(&hb_tid, NULL, heartbeat_sender, hpa);
    pthread_detach(hb_tid);

    /* Listen for connections */
    int server_sock = create_server_socket(g_client_port);
    if (server_sock < 0) {
        fprintf(stderr, "[SS] Failed to bind port %d\n", g_client_port);
        return 1;
    }
    ss_log("Listening for direct operations on port %d", g_client_port);

    while (1) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int cfd = accept(server_sock, (struct sockaddr *)&addr, &alen);
        if (cfd < 0) continue;

        ClientConnection *cc = malloc(sizeof(ClientConnection));
        cc->client_fd = cfd;
        strncpy(cc->storage_dir, g_storage_dir, MAX_PATH_LEN - 1);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_connection, cc);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
