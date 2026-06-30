/*
 * common/protocols.h — Docs++ shared protocol definitions
 *
 * Contains ALL command types, packet structs, and constants
 * used by nm_server, ss_server, and nfs_client.
 *
 * ── Existing (Y-K1n9) ──────────────────────────────────────────────────────
 *   CMD_REGISTER_SS, CMD_CLIENT_LOOKUP, CMD_LOOKUP_RESPONSE,
 *   CMD_FILE_READ, CMD_CLIENT_REGISTER, CMD_CLIENT_REGISTER_RESP,
 *   CMD_HEARTBEAT, CMD_ERROR
 *
 * ── New additions (Feature 1: CREATE, Feature 4: VIEW) ─────────────────────
 *   CMD_CREATE, CMD_CREATE_RESP, CMD_VIEW, CMD_VIEW_RESP,
 *   CMD_SS_CREATE, CMD_SS_ACK
 */

#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <stdint.h>

/* ─── Ports ──────────────────────────────────────────────────────────────── */
#define NM_PORT            5000
#define NM_HEARTBEAT_PORT  5001
#define SS_DEFAULT_PORT    6000

/* ─── Limits ─────────────────────────────────────────────────────────────── */
#define MAX_FILES          128
#define MAX_FILENAME       256
#define MAX_IP_LEN         64
#define MAX_USERNAME       64
#define MAX_MESSAGE        512
#define MAX_PATH_LEN       512
#define MAX_VIEW_FILES     256   /* max files returned in a VIEW response */

/* ─── Error Codes ────────────────────────────────────────────────────────── */
#define ERR_OK                  0
#define ERR_FILE_UNAVAILABLE   -2
#define ERR_FILE_EXISTS        -3
#define ERR_NO_SS_AVAILABLE    -4
#define ERR_INTERNAL           -5
#define ERR_NO_PERMISSION      -6
#define ERR_FILE_NOT_FOUND     -7

/* ─── Command Types ──────────────────────────────────────────────────────── */
typedef enum {
    /* ── Friend's existing commands (do NOT renumber) ── */
    CMD_REGISTER_SS          = 1,
    CMD_CLIENT_LOOKUP        = 2,
    CMD_LOOKUP_RESPONSE      = 3,
    CMD_FILE_READ            = 4,
    CMD_CLIENT_REGISTER      = 8,
    CMD_CLIENT_REGISTER_RESP = 9,
    CMD_HEARTBEAT            = 10,
    CMD_ERROR                = 11,

    /* ── Feature 1: CREATE ── */
    CMD_CREATE               = 12,   /* Client  → NM  */
    CMD_CREATE_RESP          = 13,   /* NM      → Client */
    CMD_SS_CREATE            = 14,   /* NM      → SS  */
    CMD_SS_ACK               = 15,   /* SS      → NM  */

    /* ── Feature 4: VIEW ── */
    CMD_VIEW                 = 16,   /* Client  → NM  */
    CMD_VIEW_RESP            = 17    /* NM      → Client */
} CommandType;

/* ═══════════════════════════════════════════════════════════════════════════
 *  EXISTING PACKET STRUCTS (Y-K1n9's codebase)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* SS → NM at startup: register storage server */
typedef struct {
    int32_t command_type;                      /* CMD_REGISTER_SS */
    char    ip[MAX_IP_LEN];
    int32_t nm_port;
    int32_t client_port;
    int32_t file_count;
    char    filenames[MAX_FILES][MAX_FILENAME];
} SS_Registration_Packet;

/* Client → NM: ask which SS holds <filename> */
typedef struct {
    int32_t command_type;                      /* CMD_CLIENT_LOOKUP */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} ClientLookupPacket;

/* NM → Client: reply with SS address */
typedef struct {
    int32_t status;                            /* 0 = ok */
    char    ss_ip[MAX_IP_LEN];
    int32_t ss_port;
    char    message[MAX_MESSAGE];
} LookupResponsePacket;

/* Client → NM: register username on connect */
typedef struct {
    int32_t command_type;                      /* CMD_CLIENT_REGISTER */
    char    username[MAX_USERNAME];
} ClientRegisterPacket;

/* NM → Client: registration ack */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} ClientRegisterResponse;

/* SS → Client: header for a file-read response */
typedef struct {
    int32_t command_type;                      /* CMD_FILE_READ */
    int32_t status;                            /* 0 = ok */
    int64_t content_length;                   /* bytes to follow */
} ReadResponseHeader;

/* NM → Client: heartbeat / keepalive */
typedef struct {
    int32_t command_type;                      /* CMD_HEARTBEAT */
    char    ip[MAX_IP_LEN];
    int32_t port;
} HeartbeatPacket;

/* ═══════════════════════════════════════════════════════════════════════════
 *  NEW PACKET STRUCTS — Feature 1 (CREATE) and Feature 4 (VIEW)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── CREATE ─────────────────────────────────────────────────────────────── */

/* Client → NM: create a new file */
typedef struct {
    int32_t command_type;                      /* CMD_CREATE */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} CreateRequestPacket;

/* NM → Client: result of CREATE */
typedef struct {
    int32_t status;                            /* ERR_OK or error code */
    char    message[MAX_MESSAGE];
} CreateResponsePacket;

/* NM → SS: create the physical file on disk */
typedef struct {
    int32_t command_type;                      /* CMD_SS_CREATE */
    char    filename[MAX_FILENAME];
    char    owner[MAX_USERNAME];
    char    storage_dir[MAX_PATH_LEN];         /* destination directory on SS */
} SSCreatePacket;

/* SS → NM: ack for CMD_SS_CREATE */
typedef struct {
    int32_t status;                            /* ERR_OK or error code */
    char    message[MAX_MESSAGE];
} SSCreateAck;

/* ── VIEW ────────────────────────────────────────────────────────────────── */

/* Client → NM: list files */
typedef struct {
    int32_t command_type;                      /* CMD_VIEW */
    char    username[MAX_USERNAME];
    char    flags[32];     /* "", "a", "l", "al"  (without the dash) */
} ViewRequestPacket;

/* One file entry in a VIEW response */
typedef struct {
    char    filename[MAX_FILENAME];
    char    owner[MAX_USERNAME];
    int32_t word_count;
    int32_t char_count;
    int64_t size_bytes;
    int64_t mtime;         /* unix timestamp: last modified */
    int64_t atime;         /* unix timestamp: last accessed */
    int32_t has_access;    /* 1 if requesting user has access */
} FileInfoEntry;

/* NM → Client: header for VIEW response, followed by file_count FileInfoEntry */
typedef struct {
    int32_t status;
    int32_t file_count;
} ViewResponseHeader;

#endif /* PROTOCOLS_H */
