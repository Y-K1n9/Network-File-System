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
#include <sys/types.h>
#include <unistd.h>

#define STORAGE_DIR "./ss_storage"

typedef struct {
    int client_fd;
    char storage_dir[MAX_PATH_LEN];
} ClientConnection;

typedef struct {
    char nm_ip[MAX_IP_LEN];
    int nm_heartbeat_port;
    int client_port;
} HeartbeatPingArgs;

static int contains_path_traversal(const char *path) {
    if (!path) return 0;
    if (path[0] == '/') return 1;
    if (strstr(path, "../") != NULL) return 1;
    if (strstr(path, "/..") != NULL) return 1;
    if (strcmp(path, "..") == 0) return 1;
    return 0;
}

static void handle_file_read(int fd, const char *storage_dir, const char *filename) {
    if (contains_path_traversal(filename)) {
        printf("[SS] Secure Path Block: Blocked read request for path traversal: %s\n", filename);
        ReadResponseHeader hdr = { .command_type = CMD_FILE_READ, .status = -1, .content_length = 0 };
        send_struct(fd, &hdr, sizeof(hdr));
        return;
    }

    char *buffer = NULL;
    long length = 0;
    if (read_file_into_buffer(storage_dir, filename, &buffer, &length) < 0) {
        ReadResponseHeader hdr = { .command_type = CMD_FILE_READ, .status = -1, .content_length = 0 };
        send_struct(fd, &hdr, sizeof(hdr));
        free(buffer);
        return;
    }

    ReadResponseHeader hdr;
    hdr.command_type = CMD_FILE_READ;
    hdr.status = 0;
    hdr.content_length = length;
    send_struct(fd, &hdr, sizeof(hdr));
    if (length > 0) send_all(fd, buffer, (size_t)length);
    free(buffer);
}

static void *handle_client_connection(void *arg) {
    ClientConnection *conn = (ClientConnection *)arg;
    int fd = conn->client_fd;

    int32_t cmd_type = 0;
    if (recv_all(fd, &cmd_type, sizeof(cmd_type)) < 0) {
        close(fd);
        free(conn);
        return NULL;
    }

    switch (cmd_type) {
        case CMD_FILE_READ: {
            ReadRequestPacket req;
            req.command_type = cmd_type;
            if (recv_struct(fd, ((char *)&req) + sizeof(int32_t), sizeof(req) - sizeof(int32_t)) == 0) {
                handle_file_read(fd, conn->storage_dir, req.filename);
            }
            break;
        }
        default: {
            int32_t status = -1;
            send_all(fd, &status, sizeof(status));
            break;
        }
    }

    close(fd);
    free(conn);
    return NULL;
}

static void *heartbeat_ping_thread(void *arg) {
    HeartbeatPingArgs *args = (HeartbeatPingArgs *)arg;
    while (1) {
        int fd = connect_to_server(args->nm_ip, args->nm_heartbeat_port);
        if (fd >= 0) {
            HeartbeatPacket hb;
            hb.command_type = CMD_HEARTBEAT;
            hb.client_port = args->client_port;
            send_struct(fd, &hb, sizeof(hb));
            close(fd);
        }
        usleep(1500000); // 1.5 seconds
    }
    free(args);
    return NULL;
}

static int register_with_nm(const char *nm_ip, int nm_port, int client_port, const char *storage_dir) {
    SS_Registration_Packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.command_type = CMD_REGISTER_SS;
    strncpy(packet.ip, "127.0.0.1", sizeof(packet.ip) - 1);
    packet.nm_port = nm_port;
    packet.client_port = client_port;

    if (scan_storage_directory(storage_dir, &packet) < 0) {
        fprintf(stderr, "[SS] Could not scan storage directory: %s\n", storage_dir);
        return -1;
    }

    int sockfd = connect_to_server(nm_ip, nm_port);
    if (sockfd < 0) {
        perror("[SS] connect_to_server");
        return -1;
    }

    if (send_struct(sockfd, &packet, sizeof(packet)) < 0) {
        close(sockfd);
        return -1;
    }

    printf("[SS] Registered %d files with NM at %s:%d\n", packet.file_count, nm_ip, nm_port);
    close(sockfd);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *nm_ip = "127.0.0.1";
    int nm_port = NM_PORT;
    int client_port = 6000;
    const char *storage_dir = STORAGE_DIR;

    if (argc >= 2) nm_ip = argv[1];
    if (argc >= 3) nm_port = atoi(argv[2]);
    if (argc >= 4) client_port = atoi(argv[3]);
    if (argc >= 5) storage_dir = argv[4];

    if (register_with_nm(nm_ip, nm_port, client_port, storage_dir) < 0) {
        return 1;
    }

    // Spawn heartbeat ping thread
    HeartbeatPingArgs *hb_args = (HeartbeatPingArgs *)malloc(sizeof(HeartbeatPingArgs));
    if (hb_args) {
        strncpy(hb_args->nm_ip, nm_ip, sizeof(hb_args->nm_ip) - 1);
        hb_args->nm_heartbeat_port = NM_HEARTBEAT_PORT;
        hb_args->client_port = client_port;
        pthread_t ping_tid;
        if (pthread_create(&ping_tid, NULL, heartbeat_ping_thread, hb_args) == 0) {
            pthread_detach(ping_tid);
        } else {
            free(hb_args);
        }
    }

    int server_fd = create_server_socket(client_port);
    if (server_fd < 0) {
        perror("[SS] create_server_socket");
        return 1;
    }

    printf("[SS] Listening for direct operations on port %d\n", client_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("[SS] accept");
            continue;
        }

        ClientConnection *conn = (ClientConnection *)calloc(1, sizeof(ClientConnection));
        if (!conn) {
            close(client_fd);
            continue;
        }
        conn->client_fd = client_fd;
        strncpy(conn->storage_dir, storage_dir, sizeof(conn->storage_dir) - 1);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client_connection, conn) == 0) {
            pthread_detach(tid);
        } else {
            close(client_fd);
            free(conn);
        }
    }

    close(server_fd);
    return 0;
}