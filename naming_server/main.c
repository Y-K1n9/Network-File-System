#include "storage_registry.h"
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

typedef struct {
    int client_fd;
    struct sockaddr_in addr;
} Connection;

static void *handle_connection(void *arg) {
    Connection *conn = (Connection *)arg;
    int fd = conn->client_fd;
    char peer_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &conn->addr.sin_addr, peer_ip, sizeof(peer_ip));
    free(conn);

    unsigned int type = 0;
    if (recv_packet_type(fd, &type) < 0) {
        close(fd);
        return NULL;
    }

    if (type == PKT_SS_REGISTER) {
        SS_Registration_Packet packet;
        if (recv_struct(fd, &packet, sizeof(packet)) == 0) {
            registry_upsert(&packet);
            printf("[NM] Received registration from SS at %s.\n", packet.ip);
            printf("[NM] SS client port: %d, files: %d\n", packet.client_port, packet.file_count);
        }
    } else if (type == PKT_CLIENT_LOOKUP) {
        LookupRequestPacket req;
        LookupResponsePacket resp;
        memset(&resp, 0, sizeof(resp));
        if (recv_struct(fd, &req, sizeof(req)) == 0) {
            SS_Registration_Packet ss;
            if (registry_find_file(req.filename, &ss)) {
                resp.status = 0;
                snprintf(resp.ip, sizeof(resp.ip), "%s", ss.ip);
                resp.client_port = ss.client_port;
                snprintf(resp.message, sizeof(resp.message), "File '%s' found on %s:%d", req.filename, ss.ip, ss.client_port);
                printf("[NM] Lookup for '%s' by %s resolved to %s:%d\n", req.filename, req.username, ss.ip, ss.client_port);
            } else {
                resp.status = -1;
                snprintf(resp.message, sizeof(resp.message), "File '%s' not found", req.filename);
                printf("[NM] Lookup failed for '%s' by %s\n", req.filename, req.username);
            }
            send_packet_type(fd, PKT_LOOKUP_RESPONSE);
            send_struct(fd, &resp, sizeof(resp));
        }
    } else {
        printf("[NM] Unknown packet type %u from %s\n", type, peer_ip);
    }

    close(fd);
    return NULL;
}

int main(void) {
    registry_init();

    int server_fd = create_server_socket(NM_PORT);
    if (server_fd < 0) {
        perror("[NM] Failed to create server socket");
        return 1;
    }

    printf("[NM] Listening on port %d\n", NM_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("[NM] accept");
            continue;
        }

        Connection *conn = (Connection *)malloc(sizeof(Connection));
        if (!conn) {
            close(client_fd);
            continue;
        }
        conn->client_fd = client_fd;
        conn->addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, conn) == 0) {
            pthread_detach(tid);
        } else {
            close(client_fd);
            free(conn);
        }
    }

    close(server_fd);
    return 0;
}