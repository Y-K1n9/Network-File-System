/*
 * common/utils.c — TCP utility implementation
 *
 * Reconstructed to be fully compatible with Y-K1n9/Network-File-System.
 * The functions send_all / recv_all / send_struct / recv_struct /
 * connect_to_server are used throughout nm_server, ss_server, nfs_client.
 */

#include "utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ─── send_all ───────────────────────────────────────────────────────────── */
int send_all(int sockfd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < len) {
        ssize_t sent = send(sockfd, p + total, len - total, 0);
        if (sent <= 0) return -1;
        total += (size_t)sent;
    }
    return 0;
}

/* ─── recv_all ───────────────────────────────────────────────────────────── */
int recv_all(int sockfd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t recvd = recv(sockfd, p + total, len - total, 0);
        if (recvd <= 0) return -1;
        total += (size_t)recvd;
    }
    return 0;
}

/* ─── send_struct / recv_struct ──────────────────────────────────────────── */
int send_struct(int sockfd, const void *buf, size_t len) {
    return send_all(sockfd, buf, len);
}

int recv_struct(int sockfd, void *buf, size_t len) {
    return recv_all(sockfd, buf, len);
}

/* ─── connect_to_server ──────────────────────────────────────────────────── */
int connect_to_server(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

/* ─── create_server_socket ───────────────────────────────────────────────── */
int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 32) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    return sockfd;
}
