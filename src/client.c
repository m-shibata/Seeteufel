#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BUF_SIZE 500
#define PORT "51010"

void event_loop(int fd)
{
    char msg[BUF_SIZE];
    size_t len;

    int i = 0;
    while (i++ < 10) {
        snprintf(msg, BUF_SIZE -1, "ch 50 50");
        len = strnlen(msg, BUF_SIZE - 1);
        msg[len] = '\0';
        if (write(fd, msg, len) != len) {
            fprintf(stderr, "partial/failed write\n");
        }

        sleep(2);

        snprintf(msg, BUF_SIZE -1, "ch 50 0");
        len = strnlen(msg, BUF_SIZE - 1);
        msg[len] = '\0';
        if (write(fd, msg, len) != len) {
            fprintf(stderr, "partial/failed write\n");
        }

        sleep(2);

        snprintf(msg, BUF_SIZE -1, "ch 0 50");
        len = strnlen(msg, BUF_SIZE - 1);
        msg[len] = '\0';
        if (write(fd, msg, len) != len) {
            fprintf(stderr, "partial/failed write\n");
        }

        sleep(2);

        snprintf(msg, BUF_SIZE -1, "ch -50 -50");
        len = strnlen(msg, BUF_SIZE - 1);
        msg[len] = '\0';
        if (write(fd, msg, len) != len) {
            fprintf(stderr, "partial/failed write\n");
        }

        sleep(2);

        snprintf(msg, BUF_SIZE -1, "ch 0 0");
        len = strnlen(msg, BUF_SIZE - 1);
        msg[len] = '\0';
        if (write(fd, msg, len) != len) {
            fprintf(stderr, "partial/failed write\n");
        }

        sleep(2);
    }

    strncpy(msg, "disconnect", BUF_SIZE - 1);
    len = strnlen(msg, BUF_SIZE - 1);
    msg[len] = '\0';
    if (write(fd, msg, len) != len) {
        fprintf(stderr, "partial/failed write\n");
    }
}

int main(int argc, char *argv[])
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd, s, j;
    size_t len;
    ssize_t nread;
    char buf[BUF_SIZE];

    if (argc < 2) {
        fprintf(stderr, "Usage: %s host\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    s = getaddrinfo(argv[1], PORT, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;
        close(sfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);

    event_loop(sfd);

    return 0;
}

