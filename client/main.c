/*
 * client/main.c — Docs++ CLI Client (nfs_client)
 *
 * Usage: ./nfs_client <username>
 *
 * Interactive REPL commands:
 *   CREATE <filename>                Create a new empty file
 *   VIEW [-a|-l|-al]                List files
 *   READ <filename>                 Display a file's contents
 *   STREAM <filename>               Word-by-word streaming
 *   WRITE <filename> <sentence_num> Interactive word-level editing
 *   DELETE <filename>               Delete a file (owner only)
 *   LIST                            Show connected users
 *   help / exit / quit
 */

#include "../common/protocols.h"
#include "../common/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* --- ANSI colour codes ---------------------------------------------------- */
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"
#define CLR_MAGENTA "\033[35m"
#define CLR_BLUE    "\033[34m"
#define CLR_WHITE   "\033[97m"
#define CLR_DIM     "\033[2m"

static const char *NM_IP = "127.0.0.1";

/* --- Pretty print helpers ------------------------------------------------- */
static void print_banner(const char *username) {
    printf("\n");
    printf(CLR_CYAN "╔══════════════════════════════════════════════╗\n" CLR_RESET);
    printf(CLR_CYAN "║  " CLR_BOLD CLR_WHITE "Docs++  Distributed File System Client" CLR_RESET CLR_CYAN "    ║\n" CLR_RESET);
    printf(CLR_CYAN "╚══════════════════════════════════════════════╝\n" CLR_RESET);
    printf(CLR_DIM  "  Logged in as: " CLR_RESET CLR_GREEN CLR_BOLD "%s" CLR_RESET "\n\n", username);
    printf(CLR_DIM  "  Type " CLR_RESET CLR_BOLD "help" CLR_RESET CLR_DIM " for a list of commands.\n" CLR_RESET);
    printf("\n");
}

static void print_ok(const char *msg) {
    printf(CLR_GREEN "✓ " CLR_RESET "%s\n", msg);
}

static void print_err(const char *msg) {
    printf(CLR_RED "✗ " CLR_RESET "%s\n", msg);
}

static void print_info(const char *msg) {
    printf(CLR_YELLOW "→ " CLR_RESET "%s\n", msg);
}

static void fmt_time(int64_t ts, char *buf, size_t bufsz) {
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M", tm);
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* --- CMD_CLIENT_REGISTER -------------------------------------------------- */
static int register_client(const char *username) {
    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server. Is nm_server running?");
        return -1;
    }

    ClientRegisterPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_CLIENT_REGISTER;
    strncpy(req.username, username, MAX_USERNAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        close(sockfd);
        return -1;
    }

    ClientRegisterResponse resp;
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        close(sockfd);
        return -1;
    }
    close(sockfd);

    if (resp.status != 0) {
        print_err(resp.message);
        return -1;
    }
    print_ok(resp.message);
    return 0;
}

/* --- CMD_CLIENT_LOOKUP ---------------------------------------------------- */
static int lookup_file(const char *username, const char *filename,
                        char *ss_ip, int *ss_port) {
    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return -1;
    }

    ClientLookupPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_CLIENT_LOOKUP;
    strncpy(req.username, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        close(sockfd);
        return -1;
    }

    LookupResponsePacket resp;
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        close(sockfd);
        return -1;
    }
    close(sockfd);

    if (resp.status != ERR_OK) {
        print_err(resp.message);
        return -1;
    }
    strncpy(ss_ip, resp.ss_ip, MAX_IP_LEN - 1);
    *ss_port = resp.ss_port;
    return 0;
}

/* --- cmd_read ------------------------------------------------------------- */
static void cmd_read(const char *username, const char *filename) {
    char ss_ip[MAX_IP_LEN] = {0};
    int  ss_port            = 0;

    printf(CLR_DIM "  Looking up '%s'...\n" CLR_RESET, filename);

    if (lookup_file(username, filename, ss_ip, &ss_port) < 0) return;

    int ss_fd = connect_to_server(ss_ip, ss_port);
    if (ss_fd < 0) {
        print_err("Cannot connect to Storage Server.");
        return;
    }

    int32_t cmd = CMD_FILE_READ;
    char    fname_buf[MAX_FILENAME] = {0};
    strncpy(fname_buf, filename, MAX_FILENAME - 1);

    send_all(ss_fd, &cmd,      sizeof(cmd));
    send_all(ss_fd, fname_buf, sizeof(fname_buf));

    ReadResponseHeader hdr;
    if (recv_struct(ss_fd, &hdr, sizeof(hdr)) < 0 || hdr.status != ERR_OK) {
        print_err("File not found on Storage Server.");
        close(ss_fd);
        return;
    }

    if (hdr.content_length <= 0) {
        print_info("File is empty.");
        close(ss_fd);
        return;
    }

    printf("\n" CLR_CYAN "─── %s ───────────────────────────────────\n" CLR_RESET,
           filename);

    int success = 0;
    while (1) {
        FileChunkPacket chunk;
        if (recv_struct(ss_fd, &chunk, sizeof(chunk)) < 0) break;
        if (chunk.chunk_size == 0) { success = 1; break; }
        fwrite(chunk.data, 1, (size_t)chunk.chunk_size, stdout);
        fflush(stdout);
    }
    close(ss_fd);

    if (!success) {
        printf("\n");
        print_err("Storage server went down mid-reading.");
    } else {
        printf("\n");
    }
    printf(CLR_CYAN "─────────────────────────────────────────────────\n" CLR_RESET);
}

/* --- cmd_stream ----------------------------------------------------------- */
static void cmd_stream(const char *username, const char *filename) {
    if (!filename || filename[0] == '\0') {
        print_err("Usage: STREAM <filename>");
        return;
    }

    char ss_ip[MAX_IP_LEN] = {0};
    int  ss_port            = 0;

    printf(CLR_DIM "  Looking up '%s'...\n" CLR_RESET, filename);

    if (lookup_file(username, filename, ss_ip, &ss_port) < 0) return;

    int ss_fd = connect_to_server(ss_ip, ss_port);
    if (ss_fd < 0) {
        print_err("Cannot connect to Storage Server.");
        return;
    }

    int32_t cmd = CMD_FILE_READ;
    char    fname_buf[MAX_FILENAME] = {0};
    strncpy(fname_buf, filename, MAX_FILENAME - 1);

    send_all(ss_fd, &cmd,      sizeof(cmd));
    send_all(ss_fd, fname_buf, sizeof(fname_buf));

    ReadResponseHeader hdr;
    if (recv_struct(ss_fd, &hdr, sizeof(hdr)) < 0 || hdr.status != ERR_OK) {
        print_err("File not found on Storage Server.");
        close(ss_fd);
        return;
    }

    if (hdr.content_length <= 0) {
        print_info("File is empty.");
        close(ss_fd);
        return;
    }

    printf("\n" CLR_CYAN "─── STREAM: %s ─────────────────────────────\n" CLR_RESET,
           filename);

    char word_buf[512] = {0};
    int word_len = 0;
    int success = 0;

    while (1) {
        FileChunkPacket chunk;
        if (recv_struct(ss_fd, &chunk, sizeof(chunk)) < 0) break;
        if (chunk.chunk_size == 0) { success = 1; break; }

        for (int i = 0; i < chunk.chunk_size; i++) {
            char c = chunk.data[i];
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                if (word_len > 0) {
                    word_buf[word_len] = '\0';
                    printf("%s", word_buf);
                    fflush(stdout);
                    usleep(100000);
                    word_len = 0;
                }
                putchar(c);
                fflush(stdout);
            } else {
                if (word_len < (int)sizeof(word_buf) - 1) {
                    word_buf[word_len++] = c;
                }
            }
        }
    }
    close(ss_fd);

    if (!success) {
        printf("\n");
        print_err("Storage server went down mid-streaming.");
    } else {
        if (word_len > 0) {
            word_buf[word_len] = '\0';
            printf("%s", word_buf);
            fflush(stdout);
        }
        printf("\n");
    }
    printf(CLR_CYAN "─────────────────────────────────────────────────\n" CLR_RESET);
}

/* --- cmd_delete ----------------------------------------------------------- */
static void cmd_delete(const char *username, const char *filename) {
    if (!filename || filename[0] == '\0') {
        print_err("Usage: DELETE <filename>");
        return;
    }

    printf(CLR_DIM "  Deleting file '%s'...\n" CLR_RESET, filename);

    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    DeleteRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_DELETE;
    strncpy(req.username, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send DELETE request.");
        close(sockfd);
        return;
    }

    DeleteResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }
    close(sockfd);

    if (resp.status == ERR_OK) {
        print_ok(resp.message);
    } else {
        print_err(resp.message);
    }
}

/* --- cmd_write ------------------------------------------------------------ */
static void cmd_write(const char *username, const char *filename, int sentence_number) {
    if (sentence_number < 0) {
        print_err("ERROR: Sentence index out of range.");
        return;
    }

    char ss_ip[MAX_IP_LEN] = {0};
    int ss_port = 0;

    printf(CLR_DIM "  Looking up '%s'...\n" CLR_RESET, filename);
    if (lookup_file(username, filename, ss_ip, &ss_port) < 0) return;

    int ss_fd = connect_to_server(ss_ip, ss_port);
    if (ss_fd < 0) {
        print_err("Cannot connect to Storage Server.");
        return;
    }

    WriteStartPacket start_pkt;
    memset(&start_pkt, 0, sizeof(start_pkt));
    start_pkt.command_type = CMD_WRITE;
    strncpy(start_pkt.filename, filename, MAX_FILENAME - 1);
    start_pkt.sentence_number = sentence_number;

    if (send_struct(ss_fd, &start_pkt, sizeof(start_pkt)) < 0) {
        print_err("Failed to send WRITE request.");
        close(ss_fd);
        return;
    }

    WriteStartResponse start_resp;
    if (recv_struct(ss_fd, &start_resp, sizeof(start_resp)) < 0) {
        print_err("No response from Storage Server.");
        close(ss_fd);
        return;
    }

    if (start_resp.status != ERR_OK) {
        print_err(start_resp.message);
        close(ss_fd);
        return;
    }

    char line[1024];
    while (1) {
        printf("Client: ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *trimmed = trim(line);
        if (trimmed[0] == '\0') continue;

        WriteUpdatePacket update_pkt;
        memset(&update_pkt, 0, sizeof(update_pkt));

        if (strcasecmp(trimmed, "ETIRW") == 0) {
            update_pkt.word_index = -1;
            if (send_struct(ss_fd, &update_pkt, sizeof(update_pkt)) < 0) {
                print_err("Failed to send ETIRW commit.");
                break;
            }

            WriteUpdateResponse upd_resp;
            if (recv_struct(ss_fd, &upd_resp, sizeof(upd_resp)) < 0) {
                print_err("Failed to receive commit acknowledgment.");
                break;
            }
            if (upd_resp.status == ERR_OK) {
                print_ok(upd_resp.message);
            } else {
                print_err(upd_resp.message);
            }
            break;
        } else {
            char *endptr;
            long val = strtol(trimmed, &endptr, 10);
            if (endptr == trimmed || !isspace((unsigned char)*endptr)) {
                print_err("Invalid format. Usage: <word_index> <content> or ETIRW");
                continue;
            }

            update_pkt.word_index = (int32_t)val;
            char *content = trim(endptr);
            strncpy(update_pkt.content, content, MAX_MESSAGE - 1);

            if (send_struct(ss_fd, &update_pkt, sizeof(update_pkt)) < 0) {
                print_err("Failed to send update.");
                break;
            }

            WriteUpdateResponse upd_resp;
            if (recv_struct(ss_fd, &upd_resp, sizeof(upd_resp)) < 0) {
                print_err("Failed to receive update acknowledgment.");
                break;
            }
            if (upd_resp.status != ERR_OK) {
                print_err(upd_resp.message);
                break;
            }
        }
    }
    close(ss_fd);
}

/* --- cmd_create ----------------------------------------------------------- */
static void cmd_create(const char *username, const char *filename) {
    if (!filename || filename[0] == '\0') {
        print_err("Usage: CREATE <filename>");
        return;
    }

    printf(CLR_DIM "  Creating file '%s'...\n" CLR_RESET, filename);

    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    CreateRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_CREATE;
    strncpy(req.username, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send CREATE request.");
        close(sockfd);
        return;
    }

    CreateResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }
    close(sockfd);

    if (resp.status == ERR_OK) {
        print_ok(resp.message);
    } else {
        print_err(resp.message);
    }
}

/* --- cmd_view ------------------------------------------------------------- */
static void cmd_view(const char *username, const char *flags) {
    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    ViewRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_VIEW;
    strncpy(req.username, username, MAX_USERNAME - 1);
    strncpy(req.flags,    flags,    sizeof(req.flags) - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send VIEW request.");
        close(sockfd);
        return;
    }

    ViewResponseHeader hdr;
    if (recv_struct(sockfd, &hdr, sizeof(hdr)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }

    if (hdr.status != ERR_OK) {
        print_err("Server returned an error.");
        close(sockfd);
        return;
    }

    if (hdr.file_count == 0) {
        print_info("No files found.");
        close(sockfd);
        return;
    }

    int show_long = (strchr(flags, 'l') != NULL);
    int show_all  = (strchr(flags, 'a') != NULL);

    printf("\n");
    if (show_all)
        printf(CLR_BOLD CLR_WHITE "  All files on the system:\n" CLR_RESET);
    else
        printf(CLR_BOLD CLR_WHITE "  Your files:\n" CLR_RESET);

    if (show_long) {
        printf(CLR_DIM
               "  %-28s  %-16s  %8s  %8s  %8s  %-17s  %-17s\n"
               CLR_RESET,
               "Filename", "Owner",
               "Words", "Chars", "Bytes",
               "Modified", "Accessed");
        printf(CLR_DIM
               "  %-28s  %-16s  %8s  %8s  %8s  %-17s  %-17s\n"
               CLR_RESET,
               "───────────────────────────",
               "───────────────",
               "───────", "───────", "───────",
               "─────────────────",
               "─────────────────");
    } else {
        printf(CLR_DIM "  %-28s  %-16s\n" CLR_RESET,
               "Filename", "Owner");
        printf(CLR_DIM "  %-28s  %-16s\n" CLR_RESET,
               "───────────────────────────", "───────────────");
    }

    for (int i = 0; i < hdr.file_count; ++i) {
        FileInfoEntry entry;
        if (recv_struct(sockfd, &entry, sizeof(entry)) < 0) {
            print_err("Stream cut short.");
            break;
        }

        if (show_long) {
            char mtime_s[24], atime_s[24];
            fmt_time(entry.mtime, mtime_s, sizeof(mtime_s));
            fmt_time(entry.atime, atime_s, sizeof(atime_s));

            const char *fc = strcmp(entry.owner, username) == 0
                             ? CLR_CYAN : CLR_WHITE;

            printf("  " CLR_BOLD "%s%-28s" CLR_RESET
                   "  %-16s  %8d  %8d  %8lld  %-17s  %-17s\n",
                   fc, entry.filename,
                   entry.owner,
                   entry.word_count,
                   entry.char_count,
                   (long long)entry.size_bytes,
                   mtime_s, atime_s);
        } else {
            const char *fc = strcmp(entry.owner, username) == 0
                             ? CLR_CYAN : CLR_WHITE;
            printf("  " CLR_BOLD "%s%-28s" CLR_RESET "  %s%-16s" CLR_RESET "\n",
                   fc, entry.filename,
                   CLR_DIM, entry.owner);
        }
    }

    printf("\n" CLR_DIM "  %d file(s) total.\n\n" CLR_RESET, hdr.file_count);
    close(sockfd);
}

/* --- cmd_info ------------------------------------------------------------- */
static void cmd_info(const char *username, const char *filename) {
    if (!filename || filename[0] == '\0') {
        print_err("Usage: INFO <filename>");
        return;
    }

    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    InfoRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_INFO;
    strncpy(req.username, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send INFO request.");
        close(sockfd);
        return;
    }

    InfoResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }
    close(sockfd);

    if (resp.status != ERR_OK) {
        print_err(resp.message);
        return;
    }

    char created_s[24], mtime_s[24], atime_s[24];
    fmt_time(resp.created, created_s, sizeof(created_s));
    fmt_time(resp.mtime, mtime_s, sizeof(mtime_s));
    fmt_time(resp.atime, atime_s, sizeof(atime_s));

    const char *acc_str = (resp.access_level == 2) ? "RW" : (resp.access_level == 1) ? "R" : "None";

    printf("\n" CLR_CYAN "─── %s ───────────────────────────────────\n" CLR_RESET, filename);
    printf("  Owner:          " CLR_BOLD "%s" CLR_RESET "\n", resp.owner);
    printf("  Size:           %lld bytes\n", (long long)resp.size_bytes);
    printf("  Words:          %d\n", resp.word_count);
    printf("  Characters:     %d\n", resp.char_count);
    printf("  Created:        %s\n", created_s);
    printf("  Last Modified:  %s\n", mtime_s);
    printf("  Last Accessed:  %s\n", atime_s);
    printf("  Your Access:    %s\n", acc_str);
    printf(CLR_CYAN "─────────────────────────────────────────────────\n" CLR_RESET);
}

/* --- cmd_list ------------------------------------------------------------- */
static void cmd_list(void) {
    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    ListRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_LIST;

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send LIST request.");
        close(sockfd);
        return;
    }

    ListResponseHeader hdr;
    if (recv_struct(sockfd, &hdr, sizeof(hdr)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }

    if (hdr.status != ERR_OK) {
        print_err("Server returned an error.");
        close(sockfd);
        return;
    }

    if (hdr.user_count == 0) {
        print_info("No users connected.");
        close(sockfd);
        return;
    }

    printf("\n");
    printf(CLR_BOLD CLR_WHITE "  Connected Users:\n" CLR_RESET);
    printf(CLR_DIM "  %-20s  %-16s  %-8s\n" CLR_RESET,
           "Username", "IP Address", "Status");
    printf(CLR_DIM "  %-20s  %-16s  %-8s\n" CLR_RESET,
           "───────────────────", "───────────────", "────────");

    for (int i = 0; i < hdr.user_count; ++i) {
        UserInfoEntry entry;
        if (recv_struct(sockfd, &entry, sizeof(entry)) < 0) {
            print_err("Stream cut short.");
            break;
        }

        const char *status_str = entry.is_online ? "online" : "offline";
        const char *clr = entry.is_online ? CLR_GREEN : CLR_RED;

        printf("  " CLR_BOLD CLR_CYAN "%-20s" CLR_RESET
               "  %-16s  %s%-8s" CLR_RESET "\n",
               entry.username, entry.ip, clr, status_str);
    }

    printf("\n" CLR_DIM "  %d user(s) total.\n\n" CLR_RESET, hdr.user_count);
    close(sockfd);
}

/* --- cmd_addaccess -------------------------------------------------------- */
static void cmd_addaccess(const char *username, const char *flags, const char *filename, const char *target_user) {
    int level = 0;
    if (strcasecmp(flags, "-R") == 0) level = 1;
    else if (strcasecmp(flags, "-W") == 0) level = 2;
    else {
        print_err("Invalid flag. Use -R for read, -W for read/write.");
        return;
    }

    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    AccessRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_ADDACCESS;
    strncpy(req.requester, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);
    strncpy(req.target_user, target_user, MAX_USERNAME - 1);
    req.level = level;

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send ADDACCESS request.");
        close(sockfd);
        return;
    }

    AccessResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }
    close(sockfd);

    if (resp.status == ERR_OK) {
        print_ok(resp.message);
    } else {
        print_err(resp.message);
    }
}

/* --- cmd_remaccess -------------------------------------------------------- */
static void cmd_remaccess(const char *username, const char *filename, const char *target_user) {
    int sockfd = connect_to_server(NM_IP, NM_PORT);
    if (sockfd < 0) {
        print_err("Cannot connect to Name Server.");
        return;
    }

    AccessRequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command_type = CMD_REMACCESS;
    strncpy(req.requester, username, MAX_USERNAME - 1);
    strncpy(req.filename, filename, MAX_FILENAME - 1);
    strncpy(req.target_user, target_user, MAX_USERNAME - 1);

    if (send_struct(sockfd, &req, sizeof(req)) < 0) {
        print_err("Failed to send REMACCESS request.");
        close(sockfd);
        return;
    }

    AccessResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        print_err("No response from Name Server.");
        close(sockfd);
        return;
    }
    close(sockfd);

    if (resp.status == ERR_OK) {
        print_ok(resp.message);
    } else {
        print_err(resp.message);
    }
}

/* --- Help text ------------------------------------------------------------ */
static void print_help(void) {
    printf("\n" CLR_BOLD CLR_WHITE "  Docs++ Commands\n" CLR_RESET);
    printf(CLR_CYAN "  ────────────────────────────────────────────────────\n" CLR_RESET);
    printf("  " CLR_BOLD "CREATE" CLR_RESET " <filename>    Create a new empty file on the NFS\n");
    printf("  " CLR_BOLD "VIEW" CLR_RESET "               List your accessible files\n");
    printf("  " CLR_BOLD "VIEW -a" CLR_RESET "            List ALL files on the system\n");
    printf("  " CLR_BOLD "VIEW -l" CLR_RESET "            Detailed listing (words, chars, timestamps)\n");
    printf("  " CLR_BOLD "VIEW -al" CLR_RESET "           All files with full details\n");
    printf("  " CLR_BOLD "READ" CLR_RESET " <filename>    Read and display a file's contents\n");
    printf("  " CLR_BOLD "STREAM" CLR_RESET " <filename>  Stream a file's contents word-by-word with delay\n");
    printf("  " CLR_BOLD "WRITE" CLR_RESET " <filename> <sentence_num>  Edit a file at word/sentence level\n");
    printf("  " CLR_BOLD "DELETE" CLR_RESET " <filename>  Delete a file from the NFS (owner only)\n");
    printf("  " CLR_BOLD "LIST" CLR_RESET "               Show all connected users\n");
    printf("  " CLR_BOLD "ADDACCESS" CLR_RESET " -R|-W <file> <user>  Grant read/write access to a user\n");
    printf("  " CLR_BOLD "REMACCESS" CLR_RESET " <file> <user>     Revoke access from a user\n");
    printf("  " CLR_BOLD "exit" CLR_RESET " / " CLR_BOLD "quit" CLR_RESET "       Disconnect\n");
    printf(CLR_CYAN "  ────────────────────────────────────────────────────\n\n" CLR_RESET);
}

/* --- REPL ----------------------------------------------------------------- */
static void repl(const char *username) {
    char line[512];
    while (1) {
        printf(CLR_GREEN CLR_BOLD "%s" CLR_RESET
               CLR_DIM "@docs++ " CLR_RESET CLR_YELLOW "> " CLR_RESET,
               username);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *cmd = trim(line);
        if (cmd[0] == '\0') continue;

        /* exit / quit */
        if (strcasecmp(cmd, "exit") == 0 ||
            strcasecmp(cmd, "quit") == 0) {
            printf(CLR_DIM "  Goodbye!\n" CLR_RESET);
            break;
        }

        /* help */
        if (strcasecmp(cmd, "help") == 0) {
            print_help();
            continue;
        }

        /* LIST */
        if (strcasecmp(cmd, "LIST") == 0) {
            cmd_list();
            continue;
        }

        /* CREATE <filename> */
        if (strncasecmp(cmd, "CREATE ", 7) == 0) {
            char *fname = trim(cmd + 7);
            cmd_create(username, fname);
            continue;
        }

        /* WRITE <filename> <sentence_number> */
        if (strncasecmp(cmd, "WRITE ", 6) == 0) {
            char *rest = trim(cmd + 6);
            char fname[MAX_FILENAME] = {0};

            char *space = strchr(rest, ' ');
            if (!space) {
                print_err("Usage: WRITE <filename> <sentence_number>");
                continue;
            }

            *space = '\0';
            strncpy(fname, rest, MAX_FILENAME - 1);
            char *num_str = trim(space + 1);
            if (num_str[0] == '\0') {
                print_err("Usage: WRITE <filename> <sentence_number>");
                continue;
            }

            int sentence_num = atoi(num_str);
            cmd_write(username, fname, sentence_num);
            continue;
        }

        /* VIEW [flags] */
        if (strncasecmp(cmd, "VIEW", 4) == 0) {
            char flags[32] = {0};
            char *rest = trim(cmd + 4);
            if (rest[0] == '-') {
                char *f = rest + 1;
                while (*f == '-') f++;
                strncpy(flags, f, sizeof(flags) - 1);
            }
            cmd_view(username, flags);
            continue;
        }

        /* INFO <filename> */
        if (strncasecmp(cmd, "INFO ", 5) == 0) {
            char *fname = trim(cmd + 5);
            cmd_info(username, fname);
            continue;
        }

        /* READ <filename> */
        if (strncasecmp(cmd, "READ ", 5) == 0) {
            char *fname = trim(cmd + 5);
            cmd_read(username, fname);
            continue;
        }

        /* STREAM <filename> */
        if (strncasecmp(cmd, "STREAM ", 7) == 0) {
            char *fname = trim(cmd + 7);
            cmd_stream(username, fname);
            continue;
        }

        /* DELETE <filename> */
        if (strncasecmp(cmd, "DELETE ", 7) == 0) {
            char *fname = trim(cmd + 7);
            cmd_delete(username, fname);
            continue;
        }

        /* ADDACCESS -R|-W <filename> <target_user> */
        if (strncasecmp(cmd, "ADDACCESS ", 10) == 0) {
            char *rest = trim(cmd + 10);
            char flag[16] = {0}, fname[MAX_FILENAME] = {0}, target[MAX_USERNAME] = {0};
            if (sscanf(rest, "%15s %255s %63s", flag, fname, target) == 3) {
                cmd_addaccess(username, flag, fname, target);
            } else {
                print_err("Usage: ADDACCESS -R|-W <filename> <username>");
            }
            continue;
        }

        /* REMACCESS <filename> <target_user> */
        if (strncasecmp(cmd, "REMACCESS ", 10) == 0) {
            char *rest = trim(cmd + 10);
            char fname[MAX_FILENAME] = {0}, target[MAX_USERNAME] = {0};
            if (sscanf(rest, "%255s %63s", fname, target) == 2) {
                cmd_remaccess(username, fname, target);
            } else {
                print_err("Usage: REMACCESS <filename> <username>");
            }
            continue;
        }

        printf(CLR_RED "  Unknown command." CLR_RESET
               " Type " CLR_BOLD "help" CLR_RESET " for a list of commands.\n");
    }
}

/* --- Main ----------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    print_banner(username);
    print_info("Connecting to Name Server...");

    if (register_client(username) < 0) {
        return 1;
    }

    repl(username);
    return 0;
}
