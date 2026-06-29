#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <sys/types.h>

int send_all(int sockfd, const void *buf, size_t len);
int recv_all(int sockfd, void *buf, size_t len);
int send_struct(int sockfd, const void *data, size_t len);
int recv_struct(int sockfd, void *data, size_t len);
int create_server_socket(int port);
int connect_to_server(const char *ip, int port);
void trim_newline(char *s);

#endif