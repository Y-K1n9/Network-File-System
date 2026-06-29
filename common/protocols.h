#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <stdint.h>

#define NM_PORT 5000
#define NM_HEARTBEAT_PORT 5001
#define MAX_FILES 128
#define MAX_FILENAME 256
#define MAX_IP_LEN 64
#define MAX_USERNAME 64
#define MAX_MESSAGE 512
#define MAX_PATH_LEN 512

#define ERR_FILE_UNAVAILABLE -2

typedef enum {
    CMD_REGISTER_SS = 1,
    CMD_CLIENT_LOOKUP = 2,
    CMD_LOOKUP_RESPONSE = 3,
    CMD_FILE_READ = 4,
    CMD_CLIENT_REGISTER = 8,
    CMD_CLIENT_REGISTER_RESP = 9,
    CMD_HEARTBEAT = 10,
    CMD_ERROR = 11
} CommandType;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    char ip[MAX_IP_LEN];
    int32_t nm_port;
    int32_t client_port;
    int32_t file_count;
    char filenames[MAX_FILES][MAX_FILENAME];
} SS_Registration_Packet;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
} LookupRequestPacket;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    int32_t status;
    char ip[MAX_IP_LEN];
    int32_t client_port;
    char message[MAX_MESSAGE];
} LookupResponsePacket;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
} ReadRequestPacket;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    int32_t status;
    int64_t content_length;
} ReadResponseHeader;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    int32_t client_port;
} HeartbeatPacket;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    char username[MAX_USERNAME];
} ClientRegisterPacket;

typedef struct __attribute__((packed)) {
    int32_t command_type;
    int32_t status;
    char message[MAX_MESSAGE];
} ClientRegisterResponse;

#endif