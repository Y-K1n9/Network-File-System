#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <stdint.h>

#define NM_PORT 5000
#define MAX_FILES 128
#define MAX_FILENAME 256
#define MAX_IP_LEN 64
#define MAX_USERNAME 64
#define MAX_MESSAGE 512
#define MAX_PATH_LEN 512

typedef enum {
    PKT_SS_REGISTER = 1,
    PKT_CLIENT_LOOKUP = 2,
    PKT_LOOKUP_RESPONSE = 3,
    PKT_SS_READ_REQUEST = 4,
    PKT_SS_READ_RESPONSE = 5,
    PKT_ERROR = 6
} PacketType;

typedef struct __attribute__((packed)) {
    char ip[MAX_IP_LEN];
    int32_t nm_port;
    int32_t client_port;
    int32_t file_count;
    char filenames[MAX_FILES][MAX_FILENAME];
} SS_Registration_Packet;

typedef struct __attribute__((packed)) {
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
} LookupRequestPacket;

typedef struct __attribute__((packed)) {
    int32_t status;
    char ip[MAX_IP_LEN];
    int32_t client_port;
    char message[MAX_MESSAGE];
} LookupResponsePacket;

typedef struct __attribute__((packed)) {
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
} ReadRequestPacket;

typedef struct __attribute__((packed)) {
    int32_t status;
    int64_t content_length;
} ReadResponseHeader;

#endif