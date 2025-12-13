#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>

#include "srv_defs.h"
#include "srv_routines.h"
#include "srv_sock.h"

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

    srv_sock = create_listening_socket(srv_port);
    if (srv_sock < 0) {
        p_error("Srv: main: creation of socket failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_p; i++) {
       if ((ret = fork()) == 0) {
           handle_connections_routine(srv_sock);
       }
    }

    while ((ret = wait(&status)) > 0) {
       printf("Srv: main: %d worker %s %d\n", ret,
              WIFEXITED(status) ? "exited with code" :
                                  "terminated by signal",
              WIFEXITED(status) ? WEXITSTATUS(status) :
                                  WTERMSIG(status));
    }

    return 0;
}
