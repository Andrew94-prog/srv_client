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

#include "srv_defs.h"
#include "srv_conn_queue.h"

static atomic_ulong n_conn = 0;
static unsigned long prev_n_conn = 0;

static conn_queue_t p_conn_queue;

int enable_async(int sock)
{
    if (fcntl(sock, F_SETFL, O_NONBLOCK)) {
        close(sock);
        p_error("Srv: fcntl set nonblocking failed");
        return -1;
    }

    if (fcntl(sock, F_SETSIG, SIGIO)) {
        close(sock);
        p_error("Srv: fcntl set sig failed");
        return -1;
    }

    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_ASYNC)) {
        close(sock);
        p_error("Srv: fcntl set async failed");
        return -1;
    }

    if (fcntl(sock, F_SETOWN, gettid())) {
        close(sock);
        p_error("Srv: fcntl set own failed");
        return -1;
    }

    return 0;
}

static void close_curr_conn(conn_queue_t *conn_queue)
{
    close(conn_queue->curr_conn->conn_sock);
    conn_queue->curr_conn->completed = true;
}

static int swap_to_main_ctx(conn_queue_t *conn_queue)
{
    return swapcontext(&conn_queue->curr_conn->conn_ctx,
                        &conn_queue->main_ctx);
}

static int swap_to_conn_ctx(conn_queue_t *conn_queue, conn_t *conn)
{
    conn_queue->curr_conn = conn;

    return swapcontext(&conn_queue->main_ctx, &conn->conn_ctx);
}

static void conn_handle_routine(void)
{
    char recv_buf[RECV_BUF_SIZE + 1] = {0};
    char send_buf[SEND_BUF_SIZE + 1] = "HTTP/1.1 200 OK\n"
			"Content-Type: text/html; charset=UTF-8\n"
			"Content-Length: 156\n"
			"Date: Sat, 04 Oct 2025 14:51:00 GMT\n"
			"Server: my_srv_asyn/1.0 (Ubuntu)\n"
			"\n"
			"<!DOCTYPE html>\n"
			"<html>\n"
			"<head>\n"
			"    <title>Example Page</title>\n"
			"</head>\n"
			"<body>\n"
			"    <h1>Welcome!</h1>\n"
			"    <p>This is an example HTML page.</p>\n"
			"</body>\n"
			"</html>\n";
    ssize_t to_recv = RECV_BUF_SIZE, to_send = strlen(send_buf);
    ssize_t n_recv = 0, n_send = 0, count;
    int ret;

    pr_debug("Srv: %s(): start conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);

    /* Turn new connection socket to non-blocking mode */
    if (enable_async(p_conn_queue.curr_conn->conn_sock)) {
        p_error("Srv: enabling async for conn_sock failed");
        exit(EXIT_FAILURE);
    }

    /* Receive http request from client */
    while (1) {
        n_recv = read(p_conn_queue.curr_conn->conn_sock,
                        (char *) recv_buf, to_recv);
        if (n_recv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pr_debug("Srv: %s(): recv async, switch to main_ctx, "
                         "conn_sock = %d, curr_conn = %p\n",
                         __func__, p_conn_queue.curr_conn->conn_sock,
                         p_conn_queue.curr_conn);

                ret = swap_to_main_ctx(&p_conn_queue);
                if (ret == 0) {
                    continue;
                } else {
                    p_error("Srv: switch ctx from client recv to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                p_error("Srv: recv from client failed, close connection"
                       " and switch back to main_ctx");
                close_curr_conn(&p_conn_queue);
                swap_to_main_ctx(&p_conn_queue);
            }
        } else if (n_recv == 0) {
            pr_debug("Srv: %s(): received empty buf from client,"
                     " close connection and switch back to main_ctx\n",
                     __func__);
            close_curr_conn(&p_conn_queue);
            swap_to_main_ctx(&p_conn_queue);
        } else {
            break;
        }
    }
    pr_debug("Srv: %s(): received from client: %s\n",
             __func__, recv_buf);

    /* Send http response to client */
    while (to_send) {
        count = write(p_conn_queue.curr_conn->conn_sock,
                    (char *) send_buf + n_send, to_send);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pr_debug("Srv: %s(): write async, switch to main_ctx, "
                        "conn_sock = %d, curr_conn = %p\n",
                        __func__, p_conn_queue.curr_conn->conn_sock,
                        p_conn_queue.curr_conn);

                ret = swap_to_main_ctx(&p_conn_queue);
                if (ret == 0) {
                    continue;
                } else {
                    p_error("Srv: switch ctx from client send to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                p_error("Srv: write to client failed");
                exit(EXIT_FAILURE);
            }
        } else if (n_send == 0) {
            pr_debug("Srv: %s(): sent 0 bytes to client, end\n",
                     __func__);
            break;
        } else {
            n_send += count;
            to_send -= count;
        }
    }

    if (!to_send) {
        pr_debug("Srv: %s(): response sent to client\n", __func__);
        pr_debug("Srv: %s(): end conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);
    }

    /* Close connection with client */
    close_curr_conn(&p_conn_queue);
    atomic_fetch_add(&n_conn, 1);
    swap_to_main_ctx(&p_conn_queue);
}

static int create_new_conn(int conn_sock)
{
    char *conn_stack;
    ucontext_t conn_ctx;
    conn_t *conn;

    pr_debug("Srv: %s(): new connection conn_sock = %d\n",
                __func__, conn_sock);

    /* Create context with new stack for new connection  */
    if ((conn_stack = malloc(STACK_SIZE)) == NULL) {
        close(conn_sock);
        p_error("Srv: failed to allocate stack for new connection\n");
        return -1;
    }

    getcontext(&conn_ctx);
    conn_ctx.uc_stack.ss_sp = conn_stack;
    conn_ctx.uc_stack.ss_size = STACK_SIZE;
    conn_ctx.uc_link = &p_conn_queue.main_ctx;
    makecontext(&conn_ctx, conn_handle_routine, 0);

    /* Enqueue new connection with context */
    conn = malloc(sizeof(conn_t));
    if (!conn) {
        close(conn_sock);
        p_error("Srv: allocation on ctx for new conn failed\n");
        return -1;
    }

    memcpy(&conn->conn_ctx, &conn_ctx, sizeof(ucontext_t));
    conn->conn_sock = conn_sock;
    conn->completed = false;
    enqueue_conn(&p_conn_queue, conn);

    pr_debug("Srv: %s(): created new connection conn_sock = %d,"
            " conn = %p\n", __func__, conn_sock, conn);

    return 0;
}

void process_conn_func(int srv_sock)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address), ret, conn_sock, sig;
    sigset_t sig_set;
    time_t all_start, all_end, try_start, try_end, try_time;
    conn_t *conn;

    /* Block SIGIO signal for waiting it via sigwait */
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGIO);
    sigprocmask(SIG_BLOCK, &sig_set, NULL);

    /* Init connection queue struct for current server process */
    init_conn_queue(&p_conn_queue);

    /* Set non-blocking state for listening srv socket */
    if (enable_async(srv_sock)) {
        p_error("Srv: set async state for listening socket failed");
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
    try_time = 0;
    while (1) {
        try_start = time(NULL);

        /* Accept new connection and create new socket for it */
        conn_sock = accept(srv_sock, (struct sockaddr *)&address,
                           (socklen_t *)&addrlen);

        if (conn_sock >= 0) {
            pr_debug("Srv: %s(): accepted new conn_sock = %d in cycle\n",
                     __func__, conn_sock);

            if (create_new_conn(conn_sock)) {
                p_error("Srv: create new conn failed in cycle");
                exit(EXIT_FAILURE);
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pr_debug("Srv: %s(): accept async, handle other"
                        " clients while no new connections\n",
                        __func__);
            } else {
                p_error("Srv: accept failed");
                exit(EXIT_FAILURE);
            }

            try_end = time(NULL);
            try_time += (try_end - try_start);

            if (try_time > CONN_TIMEOUT) {
                pr_debug("Srv: %s(): sleeping in cycle awaiting accept\n",
                        __func__);
                sigwait(&sig_set, &sig);
            }
        }

        while (conn = dequeue_conn(&p_conn_queue)) {
            if (ret = swap_to_conn_ctx(&p_conn_queue, conn)) {
                p_error("Srv: failed to switch to conn ctx\n");
            }

            if (!conn->completed && !ret) {
                enqueue_conn(&p_conn_queue, conn);
            } else {
                free(conn);
            }
        }

        all_end = time(NULL);


        if (all_end - all_start >= 1) {
            printf("(%d) Srv: n_conn/s = %ld\n",
                    getpid(), atomic_load(&n_conn) - prev_n_conn);
            prev_n_conn = atomic_load(&n_conn);
            all_start = all_end;
        }
    }
}
