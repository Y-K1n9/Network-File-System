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
} ReadConnection;

static void *handle_read_request(void *arg) {
    ReadConnection *conn = (ReadConnection *)arg;
    int fd = conn->client_fd;

    unsigned int type = 0;
    if (recv_packet_type(fd, &type) < 0 || type != PKT_SS_READ_REQUEST) {
        close(fd);
        free(conn);
        return NULL;
    }

    ReadRequestPacket req;
    if (recv_struct(fd, &req, sizeof(req)) < 0) {
        close(fd);
        free(conn);
        return NULL;
    }

    char *buffer = NULL;
    long length = 0;
    if (read_file_into_buffer(conn->storage_dir, req.filename, &buffer, &length) < 0) {
        ReadResponseHeader hdr = { .status = -1, .content_length = 0 };
        send_packet_type(fd, PKT_SS_READ_RESPONSE);
        send_struct(fd, &hdr, sizeof(hdr));
        free(buffer);
        close(fd);
        free(conn);
        return NULL;
    }

    ReadResponseHeader hdr;
    hdr.status = 0;
    hdr.content_length = length;
    send_packet_type(fd, PKT_SS_READ_RESPONSE);
    send_struct(fd, &hdr, sizeof(hdr));
    if (length > 0) send_all(fd, buffer, (size_t)length);

    free(buffer);
    close(fd);
    free(conn);
    return NULL;
}

static int register_with_nm(const char *nm_ip, int nm_port, int client_port, const char *storage_dir) {
    SS_Registration_Packet packet;
    memset(&packet, 0, sizeof(packet));
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

    if (send_packet_type(sockfd, PKT_SS_REGISTER) < 0 || send_struct(sockfd, &packet, sizeof(packet)) < 0) {
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

    int server_fd = create_server_socket(client_port);
    if (server_fd < 0) {
        perror("[SS] create_server_socket");
        return 1;
    }

    printf("[SS] Listening for direct reads on port %d\n", client_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("[SS] accept");
            continue;
        }

        ReadConnection *conn = (ReadConnection *)calloc(1, sizeof(ReadConnection));
        if (!conn) {
            close(client_fd);
            continue;
        }
        conn->client_fd = client_fd;
        strncpy(conn->storage_dir, storage_dir, sizeof(conn->storage_dir) - 1);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_read_request, conn) == 0) {
            pthread_detach(tid);
        } else {
            close(client_fd);
            free(conn);
        }
    }

    close(server_fd);
    return 0;
}