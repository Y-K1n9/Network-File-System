#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <sys/types.h>

int send_all(int sockfd, const void *buf, size_t len);
int recv_all(int sockfd, void *buf, size_t len);
int send_packet_type(int sockfd, unsigned int type);
int recv_packet_type(int sockfd, unsigned int *type);
int send_struct(int sockfd, const void *data, size_t len);
int recv_struct(int sockfd, void *data, size_t len);
int send_int32(int sockfd, int value);
int recv_int32(int sockfd, int *value);
int create_server_socket(int port);
int connect_to_server(const char *ip, int port);
ssize_t recv_line(int sockfd, char *buf, size_t maxlen);
void trim_newline(char *s);
const char *safe_basename(const char *path);

#endif