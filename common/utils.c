#include "utils.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

int send_packet_type(int sockfd, unsigned int type) {
    uint32_t net = htonl(type);
    return send_all(sockfd, &net, sizeof(net));
}

int recv_packet_type(int sockfd, unsigned int *type) {
    uint32_t net = 0;
    if (recv_all(sockfd, &net, sizeof(net)) < 0) return -1;
    *type = ntohl(net);
    return 0;
}

int send_struct(int sockfd, const void *data, size_t len) {
    return send_all(sockfd, data, len);
}

int recv_struct(int sockfd, void *data, size_t len) {
    return recv_all(sockfd, data, len);
}

int send_int32(int sockfd, int value) {
    int32_t net = htonl(value);
    return send_all(sockfd, &net, sizeof(net));
}

int recv_int32(int sockfd, int *value) {
    int32_t net = 0;
    if (recv_all(sockfd, &net, sizeof(net)) < 0) return -1;
    *value = ntohl(net);
    return 0;
}

int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        return -1;
    }
    return server_fd;
}

int connect_to_server(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

ssize_t recv_line(int sockfd, char *buf, size_t maxlen) {
    if (maxlen == 0) return -1;
    size_t i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n == 1) {
            buf[i++] = c;
            if (c == '\n') break;
        } else if (n == 0) {
            break;
        } else {
            return -1;
        }
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

void trim_newline(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

const char *safe_basename(const char *path) {
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}