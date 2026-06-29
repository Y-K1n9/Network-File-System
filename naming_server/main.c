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

    int32_t cmd_type = 0;
    if (recv_all(fd, &cmd_type, sizeof(cmd_type)) < 0) {
        close(fd);
        return NULL;
    }

    if (cmd_type == CMD_REGISTER_SS) {
        SS_Registration_Packet packet;
        packet.command_type = cmd_type;
        if (recv_struct(fd, ((char *)&packet) + sizeof(int32_t), sizeof(packet) - sizeof(int32_t)) == 0) {
            StorageServer *ss = registry_register_ss(packet.ip, packet.nm_port, packet.client_port);
            if (ss) {
                for (int i = 0; i < packet.file_count; ++i) {
                    registry_add_file(packet.filenames[i], ss);
                }
                printf("[NM] Received registration from SS at %s.\n", packet.ip);
                printf("[NM] SS client port: %d, files: %d\n", packet.client_port, packet.file_count);
            } else {
                printf("[NM] SS registration failed for %s:%d\n", packet.ip, packet.client_port);
            }
        }
    } else if (cmd_type == CMD_CLIENT_REGISTER) {
        ClientRegisterPacket packet;
        packet.command_type = cmd_type;
        if (recv_struct(fd, ((char *)&packet) + sizeof(int32_t), sizeof(packet) - sizeof(int32_t)) == 0) {
            int status = registry_register_client(packet.username, peer_ip);
            ClientRegisterResponse resp;
            resp.command_type = CMD_CLIENT_REGISTER_RESP;
            resp.status = status;
            if (status == 0) {
                snprintf(resp.message, sizeof(resp.message), "Welcome %s! Registration successful.", packet.username);
                printf("[NM] Client '%s' registered from %s\n", packet.username, peer_ip);
            } else {
                snprintf(resp.message, sizeof(resp.message), "Registration failed.");
            }
            send_struct(fd, &resp, sizeof(resp));
        }
    } else if (cmd_type == CMD_CLIENT_LOOKUP) {
        LookupRequestPacket req;
        req.command_type = cmd_type;
        if (recv_struct(fd, ((char *)&req) + sizeof(int32_t), sizeof(req) - sizeof(int32_t)) == 0) {
            if (!registry_is_client_registered(req.username)) {
                printf("[NM] Unregistered client '%s' attempted lookup. Dropping connection.\n", req.username);
                close(fd);
                return NULL;
            }

            LookupResponsePacket resp;
            memset(&resp, 0, sizeof(resp));
            resp.command_type = CMD_LOOKUP_RESPONSE;

            StorageServer *ss_list[MAX_REPLICAS];
            int32_t ss_count = 0;
            if (registry_find_file(req.filename, ss_list, &ss_count) && ss_count > 0) {
                StorageServer *target_ss = NULL;
                for (int i = 0; i < ss_count; ++i) {
                    if (ss_list[i]->status == SS_STATUS_ONLINE) {
                        target_ss = ss_list[i];
                        break;
                    }
                }
                if (target_ss) {
                    resp.status = 0;
                    snprintf(resp.ip, sizeof(resp.ip), "%s", target_ss->ip);
                    resp.client_port = target_ss->client_port;
                    snprintf(resp.message, sizeof(resp.message), "File '%s' found on %s:%d", req.filename, target_ss->ip, target_ss->client_port);
                    printf("[NM] Lookup for '%s' by %s resolved to %s:%d\n", req.filename, req.username, target_ss->ip, target_ss->client_port);
                } else {
                    resp.status = ERR_FILE_UNAVAILABLE;
                    snprintf(resp.message, sizeof(resp.message), "File '%s' exists but all replica storage servers are offline", req.filename);
                    printf("[NM] Lookup failed for '%s' by %s: All replica servers offline\n", req.filename, req.username);
                }
            } else {
                resp.status = -1;
                snprintf(resp.message, sizeof(resp.message), "File '%s' not found", req.filename);
                printf("[NM] Lookup failed for '%s' by %s\n", req.filename, req.username);
            }
            send_struct(fd, &resp, sizeof(resp));
        }
    } else {
        printf("[NM] Unknown command type %d from %s\n", cmd_type, peer_ip);
    }

    close(fd);
    return NULL;
}

static void *timeout_monitor_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(2);
        registry_check_timeouts(5); // timeout threshold: 5 seconds
    }
    return NULL;
}

static void *heartbeat_listener_thread(void *arg) {
    (void)arg;
    int server_fd = create_server_socket(NM_HEARTBEAT_PORT);
    if (server_fd < 0) {
        perror("[NM Heartbeat] Failed to create server socket on port 5001");
        return NULL;
    }
    printf("[NM Heartbeat] Listening for heartbeats on port %d\n", NM_HEARTBEAT_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            continue;
        }

        HeartbeatPacket hb;
        if (recv_struct(client_fd, &hb, sizeof(hb)) == 0) {
            if (hb.command_type == CMD_HEARTBEAT) {
                registry_update_heartbeat(hb.client_port);
            }
        }
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}

int main(void) {
    registry_init();

    pthread_t monitor_tid, hb_tid;
    if (pthread_create(&monitor_tid, NULL, timeout_monitor_thread, NULL) != 0) {
        perror("[NM] Failed to spawn timeout monitor thread");
        return 1;
    }
    pthread_detach(monitor_tid);

    if (pthread_create(&hb_tid, NULL, heartbeat_listener_thread, NULL) != 0) {
        perror("[NM] Failed to spawn heartbeat listener thread");
        return 1;
    }
    pthread_detach(hb_tid);

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