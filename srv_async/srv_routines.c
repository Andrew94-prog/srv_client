#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/epoll.h>

#include "srv_defs.h"
#include "srv_conn_ctxt.h"
#include "srv_sock.h"
#include "srv_send_recv.h"
#include "srv_qlist.h"
#include "srv_config.h"

static unsigned long n_conn = 0;
static unsigned long prev_n_conn = 0;

static void sigint_handler(int sig)
{
    fflush(SRV_CONFIG.log_file_desc);
    exit(128 + sig);
}

void handle_one_connection(void)
{
    LOG(LOG_INFO1, "start conn_sock = %d, conn = %p\n",
            curr_conn_sock(), curr_conn());

    /* Turn new connection socket to non-blocking mode */
    if (set_nonblock(curr_conn_sock())) {
        LOG(LOG_ERROR, "set nonblocking for conn_sock failed");
        exit(EXIT_FAILURE);
    }
    if (set_async(curr_conn_sock())) {
        LOG(LOG_ERROR, "set async for conn_sock failed");
        exit(EXIT_FAILURE);
    }

    /*
     * Handle all http requests received from client one by one
     * (http keep-alive). recv_http_msg() receives request, forms
     * response for it by make_response() and sends it to client.
     * Connection is closed when client closes it, requests
     * "Connection: close" or recv/send timeout occurs
     */
    while (handle_one_client_request()) {}

    n_conn++;

    LOG(LOG_INFO1, "end connection conn_sock = %d, conn = %p\n",
        curr_conn_sock(), curr_conn());

    /* Close connection with client */
    curr_conn_close();
    swap_to_main_ctx();
}

void handle_connections_routine(int srv_sock)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address), ret, conn_sock;
    sigset_t sig_block, sig_wait;
    struct epoll_event epoll_event;
    int epoll_timeout, epoll_fd;
    time_t all_start, all_end;
    conn_t *conn, *conn_n;

    /* Block SIGIO signal for waiting it via sigwait */
    sigemptyset(&sig_block);
    sigaddset(&sig_block, SIGIO);
    sigprocmask(SIG_BLOCK, &sig_block, NULL);
    signal(SIGIO, SIG_IGN);
    sigemptyset(&sig_wait);

    /*
     * Set new action for SIGINT signal in all workers
     * to avoid calling of SIGINT handler set by server
     * main process and flush all prints to log file
     */
    signal(SIGINT, sigint_handler);

    /* Init connection queue struct for current server process */
    init_conn_queue();
    /* Init conn ctx cache for fast allocation */
    init_conn_ctx_cache();

    /* Set non-blocking state for listening srv socket */
    if (set_nonblock(srv_sock)) {
        LOG(LOG_ERROR, "set nonblocking for listening socket failed");
        exit(EXIT_FAILURE);
    }
    /* Create epoll fd for listening srv_sock */
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        LOG(LOG_ERROR, "failed to create epoll_fd");
        exit(EXIT_FAILURE);
    }
    /* Configure epoll input events */
    epoll_event.events = EPOLLIN;
    epoll_event.data.fd = srv_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, srv_sock,
                  &epoll_event) == -1) {
        LOG(LOG_ERROR, "failed to set polling for srv_sock");
        exit(EXIT_FAILURE);
    }
    /* Start listening for incoming connections */
    if (listen(srv_sock, SOMAXCONN) < 0) {
        LOG(LOG_ERROR, "listen failed");
        exit(EXIT_FAILURE);
    }
    LOG(LOG_INFO1, "listening srv_sock %d in process %d\n",
            srv_sock, getpid());

    all_start = time(NULL);

    /* Accept incoming connections in a loop */
    while (1) {
        if (conn_queue()->active_conn_cnt) {
            epoll_timeout = 0;
        } else {
            epoll_timeout = -1;
        }

	    /* Wait for new connections on listening sokcet */
	    ret = epoll_pwait(epoll_fd, &epoll_event, 1,
                          epoll_timeout, &sig_wait);
        if (ret > 0) {
            /* Accept new connection and create new socket for it */
            conn_sock = accept(srv_sock, (struct sockaddr *)&address,
                               (socklen_t *)&addrlen);
            if (conn_sock >= 0) {
                LOG(LOG_INFO1, "accepted new conn_sock = %d "
                         "in cycle\n", conn_sock);

                if (create_new_conn(conn_sock)) {
                    LOG(LOG_ERROR, "create new conn failed in cycle");
                    exit(EXIT_FAILURE);
                }
            } else {
                LOG(LOG_INFO2, "new conn not accepted, go to "
                         "handle existing connections\n");
            }
        } else if (ret == 0) {
            LOG(LOG_INFO2, "no incoming connections, go to handle"
                    " existing connections active %d, inactive %d\n",
                    conn_queue()->active_conn_cnt,
                    conn_queue()->inactive_conn_cnt);
        } else {
            if (errno != EINTR) {
                LOG(LOG_ERROR, "epoll_pwait for srv_sock failed, errno %d\n",
                    errno);
                exit(EXIT_FAILURE);
            }
        }

        /* Handle all connections in conn queue */
        qlist_foreach_entry_safe(&conn_queue()->qconn_list, conn, conn_n, qlist) {
            if ((ret = swap_to_conn_ctx(conn))) {
                LOG(LOG_ERROR, "failed to switch to conn ctx\n");
                exit(EXIT_FAILURE);
            }

            if (conn->is_completed || ret) {
                remove_conn_from_queue(conn);
                free_closed_conn(conn);
            }
        }

        all_end = time(NULL);

        /* Measure number of connections handled per second */
        if (all_end - all_start >= 1) {
            printf("(%d) Srv: n_conn/s = %ld\n",
                    getpid(), n_conn - prev_n_conn);
            prev_n_conn = n_conn;
            all_start = all_end;
        }
    }
}
