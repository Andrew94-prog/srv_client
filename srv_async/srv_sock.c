#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <signal.h>

#include "srv_sock.h"
#include "srv_defs.h"

int create_listening_socket(int srv_port)
{
    struct sockaddr_in address;
    int srv_sock, opt = 1;

    /* Create listening socket */
    srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock == -1) {
        p_error("Srv: main: socket failed");
        return -1;
    }

    /* Forcefully attaching socket to the port */
    if (setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        p_error("Srv: main: setsockopt failed for srv_sock");
        return -1;
    }

    /* Set parameters for listening socket */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(srv_port);

    /* Bind listening socket to the address and port */
    if (bind(srv_sock, (struct sockaddr *)&address, sizeof(address))) {
        p_error("Srv: main: bind failed for srv_sock");
        return -1;
    }

    return srv_sock;
}

int set_nonblock(int sock)
{
    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK)) {
        close(sock);
        p_error("Srv: fcntl set nonblocking failed");
        return -1;
    }

    if (fcntl(sock, F_SETSIG, SIGIO)) {
        close(sock);
        p_error("Srv: fcntl set sig failed");
        return -1;
    }

    if (fcntl(sock, F_SETOWN, gettid())) {
        close(sock);
        p_error("Srv: fcntl set own failed");
        return -1;
    }

    return 0;
}

int set_async(int sock)
{
    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_ASYNC)) {
        close(sock);
        p_error("Srv: fcntl set async failed");
        return -1;
    }

    return 0;
}
