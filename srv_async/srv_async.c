#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <wait.h>
#include <time.h>
#include <ucontext.h>
#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <limits.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "srv_defs.h"
#include "srv_conn_queue.h"

static int create_listening_socket(int srv_port)
{
    struct sockaddr_in address;
    int srv_sock, opt = 1;
    int addrlen = sizeof(address);

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

int main(int argc, char *argv[])
{
    int srv_sock, srv_port = 8080, n_p = 1;
    int ret, i, status;

    if (argc >= 3) {
        n_p = atoi(argv[1]);
        srv_port = atoi(argv[2]);
    } else if (argc >= 2) {
        n_p = atoi(argv[1]);
    }

    if (n_p > MAX_NUM_WORKERS) {
        p_error("Srv: main: too many workers to create");
        exit(EXIT_FAILURE);
    }

    int worker_pids[n_p];

    srv_sock = create_listening_socket(srv_port);
    if (srv_sock < 0) {
        p_error("Srv: main: creation of socket failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_p; i++) {
       if ((ret = fork()) == 0) {
           process_conn_func(srv_sock);
       } else {
           worker_pids[i] = ret;
       }
    }

    for (i = 0; i < n_p; i++) {
       waitpid(worker_pids[i], &status, 0);
    }

    return 0;
}
