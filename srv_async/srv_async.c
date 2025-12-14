#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>

#include "srv_defs.h"
#include "srv_routines.h"
#include "srv_sock.h"
#include "srv_opts.h"

int main(int argc, char *argv[])
{
    int srv_sock, srv_port, n_w;
    int ret, i, status;

    ret = parse_srv_opts(argc, argv);
    if (ret < 0) {
        p_error("Srv: main: parse options failed\n");
        printf("%s", HELP_MSG);
        exit(EXIT_FAILURE);
    }

    if (SRV_OPTS.help) {
        printf("%s", HELP_MSG);
        return 0;
    }

    srv_port = SRV_OPTS.port;
    n_w = SRV_OPTS.num_workers;

    srv_sock = create_listening_socket(srv_port);
    if (srv_sock < 0) {
        p_error("Srv: main: creation of socket failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_w; i++) {
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
