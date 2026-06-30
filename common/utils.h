/*
 * common/utils.h — TCP send/recv helpers shared across all components
 */

#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

/* Send exactly <len> bytes — retries on partial sends */
int send_all(int sockfd, const void *buf, size_t len);

/* Receive exactly <len> bytes — retries on partial recvs */
int recv_all(int sockfd, void *buf, size_t len);

/* Convenience wrappers (same as send_all / recv_all) */
int send_struct(int sockfd, const void *buf, size_t len);
int recv_struct(int sockfd, void *buf, size_t len);

/* Connect as TCP client; returns fd or -1 */
int connect_to_server(const char *ip, int port);

/* Create a TCP server socket bound to <port>; returns fd or -1 */
int create_server_socket(int port);

#endif /* UTILS_H */
