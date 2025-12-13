#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <stdatomic.h>
#include <sys/epoll.h>

#include "srv_defs.h"
#include "srv_conn_ctxt.h"
#include "srv_sock.h"
#include "srv_send_recv.h"

static atomic_ulong n_conn = 0;
static unsigned long prev_n_conn = 0;

void handle_one_connection(void)
{
    char recv_buf[RECV_BUF_SIZE + 1] = {0};
    const char send_buf[SEND_BUF_SIZE + 1] = HTTP_RESPONSE_MSG;
    ssize_t to_recv = RECV_BUF_SIZE, to_send = strlen(send_buf);
    ssize_t n_recv, n_send;

    pr_debug("Srv: %s(): start conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);

    /* Turn new connection socket to non-blocking mode */
    if (set_nonblock(p_conn_queue.curr_conn->conn_sock)) {
        p_error("Srv: set nonblocking for conn_sock failed");
        exit(EXIT_FAILURE);
    }
    if (set_async(p_conn_queue.curr_conn->conn_sock)) {
        p_error("Srv: set async for conn_sock failed");
        exit(EXIT_FAILURE);
    }

    /* Receive http request from client */
    n_recv = recv_http_msg(recv_buf, to_recv);
    /*
     * If nothing was received from client, then close connection
     * and switch back to main_ctx
     */
    if (!n_recv) {
        pr_debug("Srv: %s(): received empty buf from client,"
                 " close connection and switch back to main_ctx\n",
                 __func__);
        curr_conn_close(&p_conn_queue);
        swap_to_main_ctx(&p_conn_queue);
    }

    pr_debug("Srv: %s(): received from client: %s\n",
             __func__, recv_buf);

    /* Send http response to client */
    n_send = send_http_msg(send_buf, to_send);
    if (n_send == to_send) {
        pr_debug("Srv: %s(): response sent to client\n", __func__);
        pr_debug("Srv: %s(): end conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);
    }

    /* Close connection with client */
    atomic_fetch_add(&n_conn, 1);
    curr_conn_close(&p_conn_queue);
    swap_to_main_ctx(&p_conn_queue);
}

void handle_connections_routine(int srv_sock)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address), ret, conn_sock;
    sigset_t sig_set;
    struct epoll_event epoll_event;
    int epoll_timeout, epoll_fd;
    time_t all_start, all_end;
    conn_t *conn, *tail_conn;

    /* Block SIGIO signal for waiting it via sigwait */
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGIO);
    sigprocmask(SIG_BLOCK, &sig_set, NULL);

    /* Init connection queue struct for current server process */
    init_conn_queue(&p_conn_queue);

    /* Set non-blocking state for listening srv socket */
    if (set_nonblock(srv_sock)) {
        p_error("Srv: set nonblocking for listening socket failed");
        exit(EXIT_FAILURE);
    }
    /* Create epoll fd for listening srv_sock */
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        p_error("Srv: failed to create epoll_fd");
        exit(EXIT_FAILURE);
    }
    /* Configure epoll input events */
    epoll_event.events = EPOLLIN;
    epoll_event.data.fd = srv_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, srv_sock,
                  &epoll_event) == -1) {
        p_error("Srv: failed to set polling for srv_sock");
        exit(EXIT_FAILURE);
    }
    /* Start listening for incoming connections */
    if (listen(srv_sock, SOMAXCONN) < 0) {
        p_error("Srv: listen failed");
        exit(EXIT_FAILURE);
    }
    pr_debug("Srv: %s(): listening srv_sock %d in process %d\n",
            __func__, srv_sock, getpid());

    all_start = time(NULL);

    /* Accept incoming connections in a loop */
    while (1) {
        if (p_conn_queue.active_conn_cnt) {
            epoll_timeout = 0;
        } else if (p_conn_queue.inactive_conn_cnt) {
            epoll_timeout = MAX_INACTIVE_TIMEOUT * 1000;
        } else {
            epoll_timeout = -1;
        }

	/* Wait for new connections on listening sokcet */
	ret = epoll_pwait(epoll_fd, &epoll_event, 1,
                          epoll_timeout, &sig_set);
        if (ret > 0) {
            /* Accept new connection and create new socket for it */
            conn_sock = accept(srv_sock, (struct sockaddr *)&address,
                               (socklen_t *)&addrlen);
            if (conn_sock >= 0) {
                pr_debug("Srv: %s(): accepted new conn_sock = %d "
                         "in cycle\n", __func__, conn_sock);

                if (create_new_conn(conn_sock)) {
                    p_error("Srv: create new conn failed in cycle");
                    exit(EXIT_FAILURE);
                }
            } else {
                pr_debug("Srv: %s(): new conn not accepted, go to "
                         "handle existing connections\n", __func__);
            }
        } else if (ret == 0) {
            pr_debug("Srv: %s(): no incoming connections, go to "
                     "handle existing connections\n", __func__);
        } else {
            p_error("Srv: epoll_pwait for srv_sock failed");
            exit(EXIT_FAILURE);
        }

        /* Handle all connections in conn queue */
        tail_conn = p_conn_queue.tail;
        while (1) {
            conn = dequeue_conn(&p_conn_queue);
            if (!conn) {
                break;
            }

            if ((ret = swap_to_conn_ctx(&p_conn_queue, conn))) {
                p_error("Srv: failed to switch to conn ctx\n");
            }

            if (!conn->is_completed && !ret) {
                enqueue_conn(&p_conn_queue, conn);
            } else {
                free_closed_conn(conn);
            }

            if (conn == tail_conn) {
                break;
            }
        }

        all_end = time(NULL);

        /* Measure number of connections handled per second */
        if (all_end - all_start >= 1) {
            printf("(%d) Srv: n_conn/s = %ld\n",
                    getpid(), atomic_load(&n_conn) - prev_n_conn);
            prev_n_conn = atomic_load(&n_conn);
            all_start = all_end;
        }
    }
}
