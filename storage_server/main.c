/*
 * storage_server/main.c — Docs++ Storage Server (ss_server)
 *
 * Usage: ./ss_server <nm_ip> <nm_port> <ss_client_port> <storage_dir>
 *
 * Dispatches on the first int32_t (command type):
 *   CMD_FILE_READ  → stream file content to client
 *   CMD_SS_CREATE  → create a new empty file
 *   CMD_SS_DELETE  → delete a file from disk
 *   CMD_WRITE      → interactive word-level editing session
 */

#include "file_handler.h"
#include "../common/protocols.h"
#include "../common/utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

/* --- Globals (set from argv) ---------------------------------------------- */
static char  g_nm_ip[MAX_IP_LEN]       = "127.0.0.1";
static int   g_nm_port                 = NM_PORT;
static int   g_client_port             = SS_DEFAULT_PORT;
static char  g_storage_dir[MAX_PATH_LEN] = "./ss_storage";

/* --- Logging -------------------------------------------------------------- */
static void ss_log(const char *fmt, ...) {
    time_t now = time(NULL);
    char ts[32];
    struct tm *t = localtime(&now);
    strftime(ts, sizeof(ts), "%H:%M:%S", t);
    printf("[SS %s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* --- Security: block path traversal --------------------------------------- */
static int contains_path_traversal(const char *path) {
    if (!path) return 0;
    if (path[0] == '/') return 1;
    if (strstr(path, "../") != NULL) return 1;
    if (strstr(path, "/..") != NULL) return 1;
    if (strcmp(path, "..") == 0) return 1;
    return 0;
}

/* --- Handler: CMD_FILE_READ ----------------------------------------------- */
static void handle_file_read(int fd, const char *filename) {
    if (contains_path_traversal(filename)) {
        ss_log("Blocked path-traversal attempt: '%s'", filename);
        ReadResponseHeader hdr = {
            .command_type   = CMD_FILE_READ,
            .status         = ERR_INTERNAL,
            .content_length = 0
        };
        send_struct(fd, &hdr, sizeof(hdr));
        return;
    }

    char *buffer = NULL;
    long  length = 0;
    if (read_file_into_buffer(g_storage_dir, filename, &buffer, &length) < 0) {
        ReadResponseHeader hdr = {
            .command_type   = CMD_FILE_READ,
            .status         = ERR_FILE_NOT_FOUND,
            .content_length = 0
        };
        send_struct(fd, &hdr, sizeof(hdr));
        ss_log("READ '%s' — file not found.", filename);
        return;
    }

    ReadResponseHeader hdr = {
        .command_type   = CMD_FILE_READ,
        .status         = ERR_OK,
        .content_length = length
    };
    send_struct(fd, &hdr, sizeof(hdr));

    if (length > 0) {
        long sent = 0;
        while (sent < length) {
            FileChunkPacket chunk;
            memset(&chunk, 0, sizeof(chunk));
            long to_send = length - sent;
            if (to_send > 256) to_send = 256;

            chunk.chunk_size = (int32_t)to_send;
            memcpy(chunk.data, buffer + sent, (size_t)to_send);

            if (send_struct(fd, &chunk, sizeof(chunk)) < 0) {
                ss_log("Error sending chunk for '%s'", filename);
                break;
            }
            sent += to_send;
        }
    }

    /* STOP packet */
    FileChunkPacket stop_chunk;
    memset(&stop_chunk, 0, sizeof(stop_chunk));
    stop_chunk.chunk_size = 0;
    send_struct(fd, &stop_chunk, sizeof(stop_chunk));

    free(buffer);
    ss_log("READ '%s' — sent %ld bytes in chunks.", filename, length);
}

/* --- Handler: CMD_SS_CREATE ----------------------------------------------- */
static void handle_ss_create(int fd, int32_t cmd_type) {
    SSCreatePacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        ss_log("Failed to receive SSCreatePacket.");
        return;
    }

    ss_log("CREATE request: file='%s' owner='%s'", req.filename, req.owner);

    SSCreateAck ack;
    memset(&ack, 0, sizeof(ack));

    if (contains_path_traversal(req.filename)) {
        ack.status = ERR_INTERNAL;
        snprintf(ack.message, sizeof(ack.message),
                 "ERROR: Invalid filename '%s'.", req.filename);
        ss_log("CREATE blocked: path traversal in '%s'.", req.filename);
        send_struct(fd, &ack, sizeof(ack));
        return;
    }

    int rc = create_empty_file(g_storage_dir, req.filename);
    if (rc < 0) {
        ack.status = ERR_FILE_EXISTS;
        snprintf(ack.message, sizeof(ack.message),
                 "ERROR: File '%s' already exists on disk.", req.filename);
        ss_log("CREATE failed: '%s' already on disk.", req.filename);
    } else {
        ack.status = ERR_OK;
        snprintf(ack.message, sizeof(ack.message),
                 "File '%s' created on SS.", req.filename);
        ss_log("CREATE OK: '%s'.", req.filename);
    }
    send_struct(fd, &ack, sizeof(ack));
}

/* --- Handler: CMD_SS_DELETE ----------------------------------------------- */
static void handle_ss_delete(int fd, int32_t cmd_type) {
    SSDeletePacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        ss_log("Failed to receive SSDeletePacket.");
        return;
    }

    ss_log("DELETE request: file='%s'", req.filename);

    SSDeleteAck ack;
    memset(&ack, 0, sizeof(ack));

    if (contains_path_traversal(req.filename)) {
        ack.status = ERR_INTERNAL;
        snprintf(ack.message, sizeof(ack.message),
                 "ERROR: Invalid filename '%s'.", req.filename);
        ss_log("DELETE blocked: path traversal in '%s'.", req.filename);
        send_struct(fd, &ack, sizeof(ack));
        return;
    }

    char path[MAX_PATH_LEN + MAX_FILENAME + 2];
    snprintf(path, sizeof(path), "%s/%s", g_storage_dir, req.filename);

    if (remove(path) != 0) {
        if (errno == ENOENT) {
            ack.status = ERR_OK;
            snprintf(ack.message, sizeof(ack.message),
                     "File '%s' was not on disk (already evicted).", req.filename);
            ss_log("DELETE: '%s' not on disk, treating as deleted.", req.filename);
        } else {
            ack.status = ERR_INTERNAL;
            snprintf(ack.message, sizeof(ack.message),
                     "ERROR: Failed to delete '%s' from disk: %s.",
                     req.filename, strerror(errno));
            ss_log("DELETE failed for '%s': %s", req.filename, strerror(errno));
        }
    } else {
        ack.status = ERR_OK;
        snprintf(ack.message, sizeof(ack.message),
                 "File '%s' deleted from SS disk.", req.filename);
        ss_log("DELETE OK: '%s' physically removed.", req.filename);
    }
    send_struct(fd, &ack, sizeof(ack));
}

/* ==========================================================================
 *  WRITE / ETIRW — word-level editing
 * ========================================================================== */

#define MAX_SENTENCE_WORDS 256
#define MAX_SENTENCES 512

typedef struct {
    char words[MAX_SENTENCE_WORDS][MAX_FILENAME];
    int  word_count;
    int  ends_with_delimiter;
} Sentence;

typedef struct {
    Sentence sentences[MAX_SENTENCES];
    int      sentence_count;
} FileContent;

static void parse_file_content(const char *text, FileContent *fc) {
    memset(fc, 0, sizeof(FileContent));
    int len = strlen(text);
    char curr_word[512];
    int curr_word_len = 0;

    int s_idx = 0;
    Sentence *curr_s = &fc->sentences[s_idx];
    curr_s->word_count = 0;
    curr_s->ends_with_delimiter = 0;

    for (int i = 0; i <= len; i++) {
        char c = text[i];

        if (c == '\0') {
            if (curr_word_len > 0) {
                curr_word[curr_word_len] = '\0';
                strncpy(curr_s->words[curr_s->word_count], curr_word, MAX_FILENAME - 1);
                curr_s->word_count++;
                curr_word_len = 0;
            }
            if (curr_s->word_count > 0) {
                s_idx++;
            }
            break;
        }

        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            if (curr_word_len > 0) {
                curr_word[curr_word_len] = '\0';
                strncpy(curr_s->words[curr_s->word_count], curr_word, MAX_FILENAME - 1);
                curr_s->word_count++;
                curr_word_len = 0;
            }
        } else if (c == '.' || c == '!' || c == '?') {
            curr_word[curr_word_len++] = c;
            curr_word[curr_word_len] = '\0';

            strncpy(curr_s->words[curr_s->word_count], curr_word, MAX_FILENAME - 1);
            curr_s->word_count++;
            curr_s->ends_with_delimiter = 1;
            curr_word_len = 0;

            s_idx++;
            if (s_idx >= MAX_SENTENCES) break;
            curr_s = &fc->sentences[s_idx];
            curr_s->word_count = 0;
            curr_s->ends_with_delimiter = 0;
        } else {
            if (curr_word_len < 511) {
                curr_word[curr_word_len++] = c;
            }
        }
    }
    fc->sentence_count = s_idx;
}

static void reconstruct_file_content(const FileContent *fc, char **out_buf, long *out_len) {
    long capacity = 4096;
    char *buf = malloc(capacity);
    long len = 0;
    buf[0] = '\0';

    for (int i = 0; i < fc->sentence_count; i++) {
        const Sentence *s = &fc->sentences[i];
        for (int j = 0; j < s->word_count; j++) {
            long word_len = strlen(s->words[j]);
            while (len + word_len + 2 >= capacity) {
                capacity *= 2;
                buf = realloc(buf, capacity);
            }
            if (len > 0) {
                buf[len++] = ' ';
            }
            memcpy(buf + len, s->words[j], word_len);
            len += word_len;
        }
    }
    buf[len] = '\0';
    *out_buf = buf;
    *out_len = len;
}

static int insert_content_at_index(FileContent *fc, int sentence_number, int word_index, const char *content) {
    FileContent *temp_fc = malloc(sizeof(FileContent));
    if (!temp_fc) return -1;
    parse_file_content(content, temp_fc);
    if (temp_fc->sentence_count == 0) {
        free(temp_fc);
        return 0;
    }

    int has_delimiter = 0;
    for (int i = 0; i < temp_fc->sentence_count; i++) {
        if (temp_fc->sentences[i].ends_with_delimiter) {
            has_delimiter = 1;
            break;
        }
    }

    if (temp_fc->sentence_count == 1 && !has_delimiter) {
        Sentence *s = &fc->sentences[sentence_number];
        int ins_words = temp_fc->sentences[0].word_count;
        if (s->word_count + ins_words >= MAX_SENTENCE_WORDS) {
            free(temp_fc);
            return -1;
        }

        memmove(&s->words[word_index + ins_words], &s->words[word_index],
                (s->word_count - word_index) * MAX_FILENAME);
        for (int i = 0; i < ins_words; i++) {
            strncpy(s->words[word_index + i], temp_fc->sentences[0].words[i], MAX_FILENAME - 1);
        }
        s->word_count += ins_words;
        free(temp_fc);
        return 0;
    } else {
        Sentence orig_s = fc->sentences[sentence_number];
        int W_orig = orig_s.word_count;

        Sentence s_before;
        memset(&s_before, 0, sizeof(s_before));
        s_before.word_count = word_index;
        for (int i = 0; i < word_index; i++) {
            strcpy(s_before.words[i], orig_s.words[i]);
        }

        Sentence s_after;
        memset(&s_after, 0, sizeof(s_after));
        s_after.word_count = W_orig - word_index;
        for (int i = 0; i < s_after.word_count; i++) {
            strcpy(s_after.words[i], orig_s.words[word_index + i]);
        }
        s_after.ends_with_delimiter = orig_s.ends_with_delimiter;

        Sentence *new_sentences = malloc(MAX_SENTENCES * sizeof(Sentence));
        if (!new_sentences) {
            free(temp_fc);
            return -1;
        }
        int new_count = 0;

        if (temp_fc->sentence_count == 1) {
            Sentence *ns0 = &new_sentences[new_count++];
            ns0->word_count = s_before.word_count + temp_fc->sentences[0].word_count;
            for (int i = 0; i < s_before.word_count; i++) strcpy(ns0->words[i], s_before.words[i]);
            for (int i = 0; i < temp_fc->sentences[0].word_count; i++) strcpy(ns0->words[s_before.word_count + i], temp_fc->sentences[0].words[i]);
            ns0->ends_with_delimiter = 1;

            if (s_after.word_count > 0) {
                Sentence *ns1 = &new_sentences[new_count++];
                ns1->word_count = s_after.word_count;
                for (int i = 0; i < s_after.word_count; i++) strcpy(ns1->words[i], s_after.words[i]);
                ns1->ends_with_delimiter = s_after.ends_with_delimiter;
            }
        } else {
            Sentence *ns0 = &new_sentences[new_count++];
            ns0->word_count = s_before.word_count + temp_fc->sentences[0].word_count;
            for (int i = 0; i < s_before.word_count; i++) strcpy(ns0->words[i], s_before.words[i]);
            for (int i = 0; i < temp_fc->sentences[0].word_count; i++) strcpy(ns0->words[s_before.word_count + i], temp_fc->sentences[0].words[i]);
            ns0->ends_with_delimiter = 1;

            for (int i = 1; i < temp_fc->sentence_count - 1; i++) {
                Sentence *nsi = &new_sentences[new_count++];
                *nsi = temp_fc->sentences[i];
            }

            Sentence *nsl = &new_sentences[new_count++];
            int last_idx = temp_fc->sentence_count - 1;
            nsl->word_count = temp_fc->sentences[last_idx].word_count + s_after.word_count;
            for (int i = 0; i < temp_fc->sentences[last_idx].word_count; i++) strcpy(nsl->words[i], temp_fc->sentences[last_idx].words[i]);
            for (int i = 0; i < s_after.word_count; i++) strcpy(nsl->words[temp_fc->sentences[last_idx].word_count + i], s_after.words[i]);
            nsl->ends_with_delimiter = (s_after.word_count > 0) ? s_after.ends_with_delimiter : temp_fc->sentences[last_idx].ends_with_delimiter;
        }

        if (fc->sentence_count + new_count - 1 >= MAX_SENTENCES) {
            free(new_sentences);
            free(temp_fc);
            return -1;
        }

        int num_to_shift = fc->sentence_count - (sentence_number + 1);
        if (num_to_shift > 0) {
            memmove(&fc->sentences[sentence_number + new_count],
                    &fc->sentences[sentence_number + 1],
                    num_to_shift * sizeof(Sentence));
        }
        for (int i = 0; i < new_count; i++) {
            fc->sentences[sentence_number + i] = new_sentences[i];
        }
        fc->sentence_count += new_count - 1;

        free(new_sentences);
        free(temp_fc);
        return 0;
    }
}

static void handle_write(int fd, int32_t cmd_type) {
    WriteStartPacket req;
    req.command_type = cmd_type;
    if (recv_struct(fd,
                    ((char *)&req) + sizeof(int32_t),
                    sizeof(req)    - sizeof(int32_t)) != 0) {
        ss_log("Failed to receive WriteStartPacket.");
        return;
    }

    ss_log("WRITE start: file='%s' sentence=%d", req.filename, req.sentence_number);

    WriteStartResponse resp;
    memset(&resp, 0, sizeof(resp));

    if (contains_path_traversal(req.filename)) {
        resp.status = ERR_INTERNAL;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: Invalid filename '%s'.", req.filename);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    char *buffer = NULL;
    long length = 0;
    if (read_file_into_buffer(g_storage_dir, req.filename, &buffer, &length) < 0) {
        resp.status = ERR_FILE_NOT_FOUND;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: File '%s' not found.", req.filename);
        send_struct(fd, &resp, sizeof(resp));
        return;
    }

    FileContent *fc = malloc(sizeof(FileContent));
    if (!fc) {
        resp.status = ERR_INTERNAL;
        snprintf(resp.message, sizeof(resp.message), "ERROR: Out of memory.");
        send_struct(fd, &resp, sizeof(resp));
        free(buffer);
        return;
    }
    parse_file_content(buffer, fc);
    free(buffer);

    int N = fc->sentence_count;
    int valid = 0;
    if (N == 0) {
        if (req.sentence_number == 0) valid = 1;
    } else {
        int ends_with_del = fc->sentences[N - 1].ends_with_delimiter;
        if (ends_with_del) {
            if (req.sentence_number >= 0 && req.sentence_number <= N) valid = 1;
        } else {
            if (req.sentence_number >= 0 && req.sentence_number < N) valid = 1;
        }
    }

    if (!valid) {
        resp.status = ERR_OUT_OF_RANGE;
        snprintf(resp.message, sizeof(resp.message),
                 "ERROR: Sentence index out of range.");
        send_struct(fd, &resp, sizeof(resp));
        free(fc);
        return;
    }

    if (req.sentence_number == N) {
        if (N >= MAX_SENTENCES) {
            resp.status = ERR_INTERNAL;
            snprintf(resp.message, sizeof(resp.message),
                     "ERROR: Max sentences reached.");
            send_struct(fd, &resp, sizeof(resp));
            free(fc);
            return;
        }
        memset(&fc->sentences[N], 0, sizeof(Sentence));
        fc->sentence_count++;
    }

    resp.status = ERR_OK;
    snprintf(resp.message, sizeof(resp.message), "OK");
    if (send_struct(fd, &resp, sizeof(resp)) < 0) {
        free(fc);
        return;
    }

    while (1) {
        WriteUpdatePacket update;
        if (recv_struct(fd, &update, sizeof(update)) < 0) {
            ss_log("Connection lost during WRITE session.");
            break;
        }

        if (update.word_index == -1) {
            /* ETIRW — commit */
            char *new_content = NULL;
            long new_len = 0;
            reconstruct_file_content(fc, &new_content, &new_len);

            char path[MAX_PATH_LEN + MAX_FILENAME + 2];
            snprintf(path, sizeof(path), "%s/%s", g_storage_dir, req.filename);
            FILE *f = fopen(path, "w");
            if (!f) {
                WriteUpdateResponse upd_resp;
                upd_resp.status = ERR_INTERNAL;
                snprintf(upd_resp.message, sizeof(upd_resp.message),
                         "ERROR: Failed to save file.");
                send_struct(fd, &upd_resp, sizeof(upd_resp));
                free(new_content);
                break;
            }
            if (new_len > 0) {
                fwrite(new_content, 1, new_len, f);
            }
            fclose(f);
            free(new_content);

            int words = 0, chars = 0;
            long bytes = 0;
            count_file_stats(g_storage_dir, req.filename, &words, &chars, &bytes);

            int nm_fd = connect_to_server(g_nm_ip, g_nm_port);
            if (nm_fd >= 0) {
                SSUpdateStatsPacket stats_pkt;
                stats_pkt.command_type = CMD_SS_UPDATE_STATS;
                strncpy(stats_pkt.filename, req.filename, MAX_FILENAME - 1);
                stats_pkt.word_count = words;
                stats_pkt.char_count = chars;
                stats_pkt.size_bytes = bytes;
                send_struct(nm_fd, &stats_pkt, sizeof(stats_pkt));
                close(nm_fd);
                ss_log("Sent updated stats for '%s' to NM: words=%d chars=%d bytes=%ld",
                       req.filename, words, chars, bytes);
            } else {
                ss_log("Warning: could not connect to NM to update stats.");
            }

            WriteUpdateResponse upd_resp;
            upd_resp.status = ERR_OK;
            snprintf(upd_resp.message, sizeof(upd_resp.message), "Write Successful!");
            send_struct(fd, &upd_resp, sizeof(upd_resp));
            break;
        } else {
            /* Word insertion */
            int word_idx = update.word_index;
            Sentence *s = &fc->sentences[req.sentence_number];
            int W = s->word_count;

            if (W == 0 && word_idx == 1) {
                word_idx = 0;
            }

            if (word_idx < 0 || word_idx > W) {
                WriteUpdateResponse upd_resp;
                upd_resp.status = ERR_OUT_OF_RANGE;
                snprintf(upd_resp.message, sizeof(upd_resp.message),
                         "ERROR: Word index out of range.");
                send_struct(fd, &upd_resp, sizeof(upd_resp));
                break;
            }

            if (insert_content_at_index(fc, req.sentence_number, word_idx, update.content) < 0) {
                WriteUpdateResponse upd_resp;
                upd_resp.status = ERR_INTERNAL;
                snprintf(upd_resp.message, sizeof(upd_resp.message),
                         "ERROR: Internal limit exceeded during insert.");
                send_struct(fd, &upd_resp, sizeof(upd_resp));
                break;
            }

            WriteUpdateResponse upd_resp;
            upd_resp.status = ERR_OK;
            snprintf(upd_resp.message, sizeof(upd_resp.message), "OK");
            if (send_struct(fd, &upd_resp, sizeof(upd_resp)) < 0) {
                break;
            }
        }
    }
    free(fc);
}

/* --- Per-connection thread ------------------------------------------------ */
static void *handle_client_connection(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    int32_t cmd_type = 0;
    if (recv_all(fd, &cmd_type, sizeof(cmd_type)) < 0) {
        close(fd);
        return NULL;
    }

    if (cmd_type == CMD_FILE_READ) {
        char filename[MAX_FILENAME] = {0};
        if (recv_all(fd, filename, sizeof(filename)) < 0) {
            close(fd);
            return NULL;
        }
        handle_file_read(fd, filename);

    } else if (cmd_type == CMD_SS_CREATE) {
        handle_ss_create(fd, cmd_type);

    } else if (cmd_type == CMD_SS_DELETE) {
        handle_ss_delete(fd, cmd_type);

    } else if (cmd_type == CMD_WRITE) {
        handle_write(fd, cmd_type);

    } else {
        ss_log("Unknown command %d — ignored.", cmd_type);
    }

    close(fd);
    return NULL;
}

/* --- Heartbeat sender ----------------------------------------------------- */
typedef struct {
    char nm_ip[MAX_IP_LEN];
    int  nm_heartbeat_port;
    int  client_port;
} HeartbeatPingArgs;

static void *heartbeat_sender(void *arg) {
    HeartbeatPingArgs *hpa = (HeartbeatPingArgs *)arg;
    HeartbeatPacket pkt;
    pkt.command_type = CMD_HEARTBEAT;
    pkt.port         = hpa->client_port;
    strncpy(pkt.ip, g_nm_ip, MAX_IP_LEN - 1);

    while (1) {
        usleep(1500000);  /* 1.5 seconds */
        int hb_fd = connect_to_server(hpa->nm_ip, hpa->nm_heartbeat_port);
        if (hb_fd >= 0) {
            send_struct(hb_fd, &pkt, sizeof(pkt));
            close(hb_fd);
        }
    }
    free(hpa);
    return NULL;
}

/* --- Register with NM ----------------------------------------------------- */
static int register_with_nm(void) {
    SS_Registration_Packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.command_type = CMD_REGISTER_SS;
    packet.nm_port      = g_client_port;
    packet.client_port  = g_client_port;

    if (scan_storage_directory(g_storage_dir, &packet) < 0) {
        ss_log("Warning: could not scan '%s' — registering with 0 files.",
               g_storage_dir);
        packet.file_count = 0;
    }

    int nm_fd = connect_to_server(g_nm_ip, g_nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "[SS] Cannot connect to NM for registration.\n");
        return -1;
    }

    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    getsockname(nm_fd, (struct sockaddr *)&local, &len);
    inet_ntop(AF_INET, &local.sin_addr, packet.ip, MAX_IP_LEN);

    if (send_struct(nm_fd, &packet, sizeof(packet)) != 0) {
        fprintf(stderr, "[SS] Failed to send registration packet.\n");
        close(nm_fd);
        return -1;
    }
    close(nm_fd);

    ss_log("Registered %d files with NM at %s:%d",
           packet.file_count, g_nm_ip, g_nm_port);
    return 0;
}

/* --- Main ----------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    if (argc > 1) strncpy(g_nm_ip,       argv[1], MAX_IP_LEN - 1);
    if (argc > 2) g_nm_port    = atoi(argv[2]);
    if (argc > 3) g_client_port = atoi(argv[3]);
    if (argc > 4) strncpy(g_storage_dir, argv[4], MAX_PATH_LEN - 1);

    struct stat st;
    if (stat(g_storage_dir, &st) != 0) {
        mkdir(g_storage_dir, 0755);
        ss_log("Created storage directory: %s", g_storage_dir);
    }

    if (register_with_nm() < 0) {
        fprintf(stderr, "[SS] Registration failed. Exiting.\n");
        return 1;
    }

    HeartbeatPingArgs *hpa = malloc(sizeof(HeartbeatPingArgs));
    strncpy(hpa->nm_ip, g_nm_ip, MAX_IP_LEN - 1);
    hpa->nm_heartbeat_port = NM_HEARTBEAT_PORT;
    hpa->client_port       = g_client_port;
    pthread_t hb_tid;
    pthread_create(&hb_tid, NULL, heartbeat_sender, hpa);
    pthread_detach(hb_tid);

    int server_sock = create_server_socket(g_client_port);
    if (server_sock < 0) {
        fprintf(stderr, "[SS] Failed to bind port %d\n", g_client_port);
        return 1;
    }
    ss_log("Listening for direct operations on port %d", g_client_port);

    while (1) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int cfd = accept(server_sock, (struct sockaddr *)&addr, &alen);
        if (cfd < 0) continue;

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = cfd;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_connection, fd_ptr);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
