/*
 * common/protocols.h — Docs++ shared protocol definitions
 *
 * All command types, packet structs, error codes, and constants
 * used by nm_server, ss_server, and nfs_client.
 */

#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <stdint.h>

/* --- Ports ---------------------------------------------------------------- */
#define NM_PORT            5000
#define NM_HEARTBEAT_PORT  5001
#define SS_DEFAULT_PORT    6000

/* --- Limits --------------------------------------------------------------- */
#define MAX_FILES          128
#define MAX_FILENAME       256
#define MAX_IP_LEN         64
#define MAX_USERNAME       64
#define MAX_MESSAGE        512
#define MAX_PATH_LEN       512
#define MAX_VIEW_FILES     256

/* --- Error Codes ---------------------------------------------------------- */
#define ERR_OK               0   /* Success                              */
#define ERR_NO_PERMISSION   -1   /* Unauthorized access                  */
#define ERR_FILE_NOT_FOUND  -2   /* File does not exist                  */
#define ERR_FILE_EXISTS     -3   /* File already exists                  */
#define ERR_FILE_UNAVAILABLE -4  /* SS hosting file is offline           */
#define ERR_NO_SS_AVAILABLE -5   /* No storage server online             */
#define ERR_INTERNAL        -6   /* Internal / system failure            */
#define ERR_OUT_OF_RANGE    -7   /* Index out of bounds                  */
#define ERR_MAX_CLIENTS     -8   /* Max connected clients reached        */
#define ERR_MAX_FILES       -9   /* Max files reached                    */
#define ERR_FILE_LOCKED    -10   /* File locked for writing (future)     */
#define ERR_NETWORK        -11   /* Network / connection failure         */

/* --- Command Types -------------------------------------------------------- */
typedef enum {
    CMD_REGISTER_SS          = 1,
    CMD_CLIENT_LOOKUP        = 2,
    CMD_LOOKUP_RESPONSE      = 3,
    CMD_FILE_READ            = 4,
    CMD_CLIENT_REGISTER      = 8,
    CMD_CLIENT_REGISTER_RESP = 9,
    CMD_HEARTBEAT            = 10,
    CMD_ERROR                = 11,

    /* CREATE */
    CMD_CREATE               = 12,  /* Client → NM   */
    CMD_CREATE_RESP          = 13,  /* NM     → Client */
    CMD_SS_CREATE            = 14,  /* NM     → SS   */
    CMD_SS_ACK               = 15,  /* SS     → NM   */

    /* VIEW */
    CMD_VIEW                 = 16,  /* Client → NM   */
    CMD_VIEW_RESP            = 17,  /* NM     → Client */

    /* DELETE */
    CMD_DELETE               = 18,  /* Client → NM   */
    CMD_DELETE_RESP          = 19,  /* NM     → Client */
    CMD_SS_DELETE            = 20,  /* NM     → SS   */
    CMD_SS_DELETE_ACK        = 21,  /* SS     → NM   */

    /* WRITE / ETIRW */
    CMD_WRITE                = 22,  /* Client → SS   */
    CMD_WRITE_RESP           = 23,  /* SS     → Client */
    CMD_SS_UPDATE_STATS      = 24,  /* SS     → NM   */

    /* LIST */
    CMD_LIST                 = 25,  /* Client → NM   */
    CMD_LIST_RESP            = 26,  /* NM     → Client */

    /* INFO */
    CMD_INFO                 = 27,  /* Client → NM   */
    CMD_INFO_RESP            = 28,  /* NM     → Client */

    /* ACCESS CONTROL */
    CMD_ADDACCESS            = 29,  /* Client → NM   */
    CMD_ADDACCESS_RESP       = 30,  /* NM     → Client */
    CMD_REMACCESS            = 31,  /* Client → NM   */
    CMD_REMACCESS_RESP       = 32   /* NM     → Client */
} CommandType;

/* ==========================================================================
 *  Packet structs — Registration & Lookup
 * ========================================================================== */

/* SS → NM: register storage server */
typedef struct {
    int32_t command_type;                      /* CMD_REGISTER_SS */
    char    ip[MAX_IP_LEN];
    int32_t nm_port;
    int32_t client_port;
    int32_t file_count;
    char    filenames[MAX_FILES][MAX_FILENAME];
} SS_Registration_Packet;

/* Client → NM: lookup which SS holds a file */
typedef struct {
    int32_t command_type;                      /* CMD_CLIENT_LOOKUP */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} ClientLookupPacket;

/* NM → Client: reply with SS address */
typedef struct {
    int32_t status;
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
    int32_t status;
    int64_t content_length;
} ReadResponseHeader;

/* SS heartbeat keepalive */
typedef struct {
    int32_t command_type;                      /* CMD_HEARTBEAT */
    char    ip[MAX_IP_LEN];
    int32_t port;
} HeartbeatPacket;

/* ==========================================================================
 *  Packet structs — CREATE
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_CREATE */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} CreateRequestPacket;

/* NM → Client */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} CreateResponsePacket;

/* NM → SS: create physical file */
typedef struct {
    int32_t command_type;                      /* CMD_SS_CREATE */
    char    filename[MAX_FILENAME];
    char    owner[MAX_USERNAME];
    char    storage_dir[MAX_PATH_LEN];
} SSCreatePacket;

/* SS → NM: ack */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} SSCreateAck;

/* ==========================================================================
 *  Packet structs — VIEW
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_VIEW */
    char    username[MAX_USERNAME];
    char    flags[32];                         /* "", "a", "l", "al" */
} ViewRequestPacket;

/* One file entry in VIEW response */
typedef struct {
    char    filename[MAX_FILENAME];
    char    owner[MAX_USERNAME];
    int32_t word_count;
    int32_t char_count;
    int64_t size_bytes;
    int64_t mtime;
    int64_t atime;
    int32_t has_access;
} FileInfoEntry;

/* NM → Client: header, followed by file_count × FileInfoEntry */
typedef struct {
    int32_t status;
    int32_t file_count;
} ViewResponseHeader;

/* ==========================================================================
 *  Packet structs — Direct Streaming Chunks
 * ========================================================================== */

typedef struct {
    int32_t chunk_size;                        /* 0 = STOP */
    char    data[256];
} FileChunkPacket;

/* ==========================================================================
 *  Packet structs — DELETE
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_DELETE */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} DeleteRequestPacket;

/* NM → Client */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} DeleteResponsePacket;

/* NM → SS: delete physical file */
typedef struct {
    int32_t command_type;                      /* CMD_SS_DELETE */
    char    filename[MAX_FILENAME];
} SSDeletePacket;

/* SS → NM: ack */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} SSDeleteAck;

/* ==========================================================================
 *  Packet structs — WRITE / ETIRW
 * ========================================================================== */

/* Client → SS: start a write session */
typedef struct {
    int32_t command_type;                      /* CMD_WRITE */
    char    filename[MAX_FILENAME];
    int32_t sentence_number;
} WriteStartPacket;

/* SS → Client: write session ack */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} WriteStartResponse;

/* Client → SS: word update (-1 = ETIRW commit) */
typedef struct {
    int32_t word_index;
    char    content[MAX_MESSAGE];
} WriteUpdatePacket;

/* SS → Client: update ack */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} WriteUpdateResponse;

/* SS → NM: push updated file stats after write */
typedef struct {
    int32_t command_type;                      /* CMD_SS_UPDATE_STATS */
    char    filename[MAX_FILENAME];
    int32_t word_count;
    int32_t char_count;
    int64_t size_bytes;
} SSUpdateStatsPacket;

/* ==========================================================================
 *  Packet structs — LIST (connected users)
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_LIST */
} ListRequestPacket;

/* NM → Client: header, followed by user_count × UserInfoEntry */
typedef struct {
    int32_t status;
    int32_t user_count;
} ListResponseHeader;

/* One user entry */
typedef struct {
    char    username[MAX_USERNAME];
    char    ip[MAX_IP_LEN];
    int32_t is_online;
} UserInfoEntry;

/* ==========================================================================
 *  Packet structs — INFO
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_INFO */
    char    username[MAX_USERNAME];
    char    filename[MAX_FILENAME];
} InfoRequestPacket;

/* NM → Client */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
    int64_t size_bytes;
    int32_t word_count;
    int32_t char_count;
    int64_t created;
    int64_t mtime;
    int64_t atime;
    char    owner[MAX_USERNAME];
    int32_t access_level;
} InfoResponsePacket;

/* ==========================================================================
 *  Packet structs — ACCESS CONTROL
 * ========================================================================== */

/* Client → NM */
typedef struct {
    int32_t command_type;                      /* CMD_ADDACCESS or CMD_REMACCESS */
    char    requester[MAX_USERNAME];
    char    filename[MAX_FILENAME];
    char    target_user[MAX_USERNAME];
    int32_t level;                             /* 1=Read, 2=Read+Write */
} AccessRequestPacket;

/* NM → Client */
typedef struct {
    int32_t status;
    char    message[MAX_MESSAGE];
} AccessResponsePacket;

#endif /* PROTOCOLS_H */
