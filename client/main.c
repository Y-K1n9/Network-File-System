#include "../common/protocols.h"
#include "../common/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int lookup_file(const char *username, const char *filename, char *ss_ip, int *ss_port) {
    int sockfd = connect_to_server("127.0.0.1", NM_PORT);
    if (sockfd < 0) {
        perror("[Client] connect_to_server");
        return -1;
    }

    LookupRequestPacket req;
    memset(&req, 0, sizeof(req));
    snprintf(req.username, sizeof(req.username), "%s", username);
    snprintf(req.filename, sizeof(req.filename), "%s", filename);

    if (send_packet_type(sockfd, PKT_CLIENT_LOOKUP) < 0 || send_struct(sockfd, &req, sizeof(req)) < 0) {
        close(sockfd);
        return -1;
    }

    unsigned int type = 0;
    if (recv_packet_type(sockfd, &type) < 0 || type != PKT_LOOKUP_RESPONSE) {
        close(sockfd);
        return -1;
    }

    LookupResponsePacket resp;
    if (recv_struct(sockfd, &resp, sizeof(resp)) < 0) {
        close(sockfd);
        return -1;
    }

    close(sockfd);

    if (resp.status != 0) {
        printf("%s\n", resp.message);
        return -1;
    }

    snprintf(ss_ip, MAX_IP_LEN, "%s", resp.ip);
    *ss_port = resp.client_port;
    printf("[Client] NM response: %s\n", resp.message);
    return 0;
}

static int read_file_from_ss(const char *ss_ip, int ss_port, const char *username, const char *filename) {
    int sockfd = connect_to_server(ss_ip, ss_port);
    if (sockfd < 0) {
        perror("[Client] connect_to_server SS");
        return -1;
    }

    ReadRequestPacket req;
    memset(&req, 0, sizeof(req));
    snprintf(req.username, sizeof(req.username), "%s", username);
    snprintf(req.filename, sizeof(req.filename), "%s", filename);

    if (send_packet_type(sockfd, PKT_SS_READ_REQUEST) < 0 || send_struct(sockfd, &req, sizeof(req)) < 0) {
        close(sockfd);
        return -1;
    }

    unsigned int type = 0;
    if (recv_packet_type(sockfd, &type) < 0 || type != PKT_SS_READ_RESPONSE) {
        close(sockfd);
        return -1;
    }

    ReadResponseHeader hdr;
    if (recv_struct(sockfd, &hdr, sizeof(hdr)) < 0) {
        close(sockfd);
        return -1;
    }

    if (hdr.status != 0) {
        printf("[Client] Storage server could not read '%s'\n", filename);
        close(sockfd);
        return -1;
    }

    char *buffer = (char *)malloc((size_t)hdr.content_length + 1);
    if (!buffer) {
        close(sockfd);
        return -1;
    }

    if (hdr.content_length > 0 && recv_all(sockfd, buffer, (size_t)hdr.content_length) < 0) {
        free(buffer);
        close(sockfd);
        return -1;
    }
    buffer[hdr.content_length] = '\0';

    printf("%s\n", buffer);

    free(buffer);
    close(sockfd);
    return 0;
}

int main(void) {
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];

    printf("Username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) return 1;
    trim_newline(username);

    printf("File to read: ");
    fflush(stdout);
    if (!fgets(filename, sizeof(filename), stdin)) return 1;
    trim_newline(filename);

    char ss_ip[MAX_IP_LEN] = {0};
    int ss_port = 0;

    if (lookup_file(username, filename, ss_ip, &ss_port) < 0) {
        return 1;
    }

    return read_file_from_ss(ss_ip, ss_port, username, filename) == 0 ? 0 : 1;
}