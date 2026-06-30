/*
 * client/main.c — Docs++ CLI Client (nfs_client)
 *
 * Usage:
 *   ./nfs_client <username>
 *
 * Interactive REPL — type commands after the prompt:
 *
 *   CREATE <filename>         Create a new empty file  [Feature 1]
 *   VIEW                      List your files          [Feature 4]
 *   VIEW -a                   List ALL files
 *   VIEW -l                   List your files (detailed)
 *   VIEW -al                  List all files (detailed)
 *   READ <filename>           Download and display a file's contents
 *   help                      Show help
 *   exit / quit               Exit
 *
 * Each command opens a fresh TCP connection to NM_PORT (5000),
 * following Y-K1n9's one-connection-per-command pattern.
 *
 * Compile (as part of Makefile target nfs_client):
 *   gcc -Wall -O2 -pthread \
 *       client/main.c common/utils.c \
 *       -o nfs_client
 */

#include "../common/protocols.h"
#include "../common/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ─── ANSI colour codes ──────────────────────────────────────────────────── */
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

/* ─── Pretty print helpers ───────────────────────────────────────────────── */
static void print_banner(const char *username) {
    printf("\n");
    printf(CLR_CYAN "╔══════════════════════════════════════════════╗\n" CLR_RESET);
    printf(CLR_CYAN "║  " CLR_BOLD CLR_WHITE "Docs++  Distributed File System Client" CLR_RESET CLR_CYAN "    ║\n" CLR_RESET);
    printf(CLR_CYAN "╚══════════════════════════════════════════════╝\n" CLR_RESET);
    printf(CLR_DIM  "  Logged in as: " CLR_RESET CLR_GREEN CLR_BOLD "%s" CLR_RESET "\n\n", username);
    printf(CLR_DIM  "  Commands:  CREATE <file>  VIEW [-a|-l|-al]  READ <file>  help  exit\n" CLR_RESET);
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

/* Format unix timestamp as "YYYY-MM-DD HH:MM" */
static void fmt_time(int64_t ts, char *buf, size_t bufsz) {
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M", tm);
}

/* ─── Trim leading whitespace ─────────────────────────────────────────────── */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  CMD_CLIENT_REGISTER  (Y-K1n9 original)
 *  Handshake: identify ourselves to NM at startup.
 * ═══════════════════════════════════════════════════════════════════════════ */
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

/* ═══════════════════════════════════════════════════════════════════════════
 *  CMD_CLIENT_LOOKUP  (Y-K1n9 original)
 * ═══════════════════════════════════════════════════════════════════════════ */
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

/* ═══════════════════════════════════════════════════════════════════════════
 *  READ command — lookup + read from SS directly  (Y-K1n9 flow)
 * ═══════════════════════════════════════════════════════════════════════════ */
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

    /* Send command type + filename */
    int32_t cmd = CMD_FILE_READ;
    char    fname_buf[MAX_FILENAME] = {0};
    strncpy(fname_buf, filename, MAX_FILENAME - 1);

    send_all(ss_fd, &cmd,      sizeof(cmd));
    send_all(ss_fd, fname_buf, sizeof(fname_buf));

    /* Receive header */
    ReadResponseHeader hdr;
    if (recv_struct(ss_fd, &hdr, sizeof(hdr)) < 0 || hdr.status != ERR_OK) {
        print_err("File not found on Storage Server.");
        close(ss_fd);
        return;
    }

    /* Receive content */
    if (hdr.content_length <= 0) {
        print_info("File is empty.");
        close(ss_fd);
        return;
    }

    char *buf = malloc((size_t)hdr.content_length + 1);
    if (!buf) { close(ss_fd); return; }
    recv_all(ss_fd, buf, (size_t)hdr.content_length);
    buf[hdr.content_length] = '\0';
    close(ss_fd);

    printf("\n" CLR_CYAN "─── %s ───────────────────────────────────\n" CLR_RESET,
           filename);
    printf("%s", buf);
    if (buf[hdr.content_length - 1] != '\n') printf("\n");
    printf(CLR_CYAN "─────────────────────────────────────────────────\n" CLR_RESET);
    free(buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  CREATE command  [Feature 1 — NEW]
 *
 *  Client → NM: CreateRequestPacket
 *  NM → Client: CreateResponsePacket
 * ═══════════════════════════════════════════════════════════════════════════ */
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

/* ═══════════════════════════════════════════════════════════════════════════
 *  VIEW command  [Feature 4 — NEW]
 *
 *  Client → NM: ViewRequestPacket (with flags)
 *  NM → Client: ViewResponseHeader + N × FileInfoEntry
 *
 *  Flags (without '-'):
 *    ""   or nothing → your files, names only
 *    "a"             → all files, names only
 *    "l"             → your files, detailed table
 *    "al"            → all files, detailed table
 * ═══════════════════════════════════════════════════════════════════════════ */
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

    /* Table header */
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

    /* Receive and display each entry */
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

            /* Colour own files cyan, others white */
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

/* ─── Help text ──────────────────────────────────────────────────────────── */
static void print_help(void) {
    printf("\n" CLR_BOLD CLR_WHITE "  Docs++ Commands\n" CLR_RESET);
    printf(CLR_CYAN "  ────────────────────────────────────────────────────\n" CLR_RESET);
    printf("  " CLR_BOLD "CREATE" CLR_RESET " <filename>    Create a new empty file on the NFS\n");
    printf("  " CLR_BOLD "VIEW" CLR_RESET "               List your accessible files\n");
    printf("  " CLR_BOLD "VIEW -a" CLR_RESET "            List ALL files on the system\n");
    printf("  " CLR_BOLD "VIEW -l" CLR_RESET "            Detailed listing (words, chars, timestamps)\n");
    printf("  " CLR_BOLD "VIEW -al" CLR_RESET "           All files with full details\n");
    printf("  " CLR_BOLD "READ" CLR_RESET " <filename>    Read and display a file's contents\n");
    printf("  " CLR_BOLD "exit" CLR_RESET " / " CLR_BOLD "quit" CLR_RESET "       Disconnect\n");
    printf(CLR_CYAN "  ────────────────────────────────────────────────────\n\n" CLR_RESET);
}

/* ─── REPL ───────────────────────────────────────────────────────────────── */
static void repl(const char *username) {
    char line[512];
    while (1) {
        printf(CLR_GREEN CLR_BOLD "%s" CLR_RESET
               CLR_DIM "@docs++ " CLR_RESET CLR_YELLOW "> " CLR_RESET,
               username);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *cmd = trim(line);
        if (cmd[0] == '\0') continue;

        /* ── exit / quit ── */
        if (strcasecmp(cmd, "exit") == 0 ||
            strcasecmp(cmd, "quit") == 0) {
            printf(CLR_DIM "  Goodbye!\n" CLR_RESET);
            break;
        }

        /* ── help ── */
        if (strcasecmp(cmd, "help") == 0) {
            print_help();
            continue;
        }

        /* ── CREATE <filename> ── */
        if (strncasecmp(cmd, "CREATE ", 7) == 0) {
            char *fname = trim(cmd + 7);
            cmd_create(username, fname);
            continue;
        }

        /* ── VIEW [flags] ── */
        if (strncasecmp(cmd, "VIEW", 4) == 0) {
            char flags[32] = {0};
            char *rest = trim(cmd + 4);
            /* rest could be "", "-a", "-l", "-al", "-la" */
            if (rest[0] == '-') {
                /* strip the leading '-' and any additional '-' */
                char *f = rest + 1;
                while (*f == '-') f++;
                strncpy(flags, f, sizeof(flags) - 1);
                /* normalise "la" → "al" doesn't matter, just copy */
            }
            cmd_view(username, flags);
            continue;
        }

        /* ── READ <filename> ── */
        if (strncasecmp(cmd, "READ ", 5) == 0) {
            char *fname = trim(cmd + 5);
            cmd_read(username, fname);
            continue;
        }

        printf(CLR_RED "  Unknown command." CLR_RESET
               " Type " CLR_BOLD "help" CLR_RESET " for a list of commands.\n");
    }
}

/* ─── Main ───────────────────────────────────────────────────────────────── */
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
