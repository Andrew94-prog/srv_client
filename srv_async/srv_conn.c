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

static void curr_conn_close(conn_queue_t *conn_queue)
{
    close(conn_queue->curr_conn->conn_sock);
    conn_queue->curr_conn->is_completed = true;
}

static void curr_conn_keep_active(conn_queue_t *conn_queue)
{
    conn_queue->curr_conn->is_active = true;
}

static void curr_conn_keep_inactive(conn_queue_t *conn_queue)
{
    conn_queue->curr_conn->is_active = false;
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

static bool is_http_message_end(char *recv_buf, ssize_t n_recv)
{
    return n_recv >= 4 && recv_buf[n_recv - 1] == '\n' &&
                    recv_buf[n_recv - 2] == '\r' &&
                    recv_buf[n_recv - 3] == '\n' &&
                    recv_buf[n_recv - 4] == '\r';
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
    time_t start_t, end_t;
    bool completed;
    int ret;

    pr_debug("Srv: %s(): start conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);

    /* Turn new connection socket to non-blocking mode */
    if (enable_async(p_conn_queue.curr_conn->conn_sock)) {
        p_error("Srv: enabling async for conn_sock failed");
        exit(EXIT_FAILURE);
    }

    /* Receive http request from client */
    completed = false;
    start_t = time(NULL);
    while (to_recv && !completed) {
        count = read(p_conn_queue.curr_conn->conn_sock,
                        (char *) recv_buf + n_recv, to_recv);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                end_t = time(NULL);
                if (end_t - start_t > MAX_ACTIVE_TIMEOUT) {
                    pr_debug("Srv: %s(): recv async, keep inactivate "
                            "conn_sock = %d, curr_conn = %p\n",
                            __func__, p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_keep_inactive(&p_conn_queue);
                }

                if (end_t - start_t > MAX_INACTIVE_TIMEOUT) {
                    pr_debug("Srv: %s(): recv async, close "
                            "conn_sock = %d, curr_conn = %p\n",
                            __func__, p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_close(&p_conn_queue);
                }


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
                curr_conn_close(&p_conn_queue);
                swap_to_main_ctx(&p_conn_queue);
            }
        } else if (count > 0) {
            n_recv += count;
            to_recv -= count;

            curr_conn_keep_active(&p_conn_queue);
            completed = is_http_message_end(recv_buf, n_recv);
        } else {
            completed = true;
            curr_conn_keep_active(&p_conn_queue);
        }
    }

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
    completed = false;
    start_t = time(NULL);
    while (to_send && !completed) {
        count = write(p_conn_queue.curr_conn->conn_sock,
                    (char *) send_buf + n_send, to_send);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                end_t = time(NULL);
                if (end_t - start_t > MAX_ACTIVE_TIMEOUT) {
                    pr_debug("Srv: %s(): send async, keep inactive "
                            "conn_sock = %d, curr_conn = %p\n",
                            __func__, p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_keep_inactive(&p_conn_queue);
                }

                if (end_t - start_t > MAX_INACTIVE_TIMEOUT) {
                    pr_debug("Srv: %s(): send async, close "
                            "conn_sock = %d, curr_conn = %p\n",
                            __func__, p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_close(&p_conn_queue);
                }


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
        } else if (count > 0) {
            n_send += count;
            to_send -= count;
            curr_conn_keep_active(&p_conn_queue);
        } else {
            pr_debug("Srv: %s(): sent 0 bytes to client, end\n",
                     __func__);
            completed = true;
            curr_conn_keep_active(&p_conn_queue);
        }
    }

    if (!to_send) {
        pr_debug("Srv: %s(): response sent to client\n", __func__);
        pr_debug("Srv: %s(): end conn_sock = %d, conn = %p\n", __func__,
            p_conn_queue.curr_conn->conn_sock, p_conn_queue.curr_conn);
    }

    /* Close connection with client */
    atomic_fetch_add(&n_conn, 1);
    curr_conn_close(&p_conn_queue);
    swap_to_main_ctx(&p_conn_queue);
}

static void free_closed_conn(conn_t *conn)
{
    free(conn->conn_ctx.uc_stack.ss_sp);
    free(conn);
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
    conn->is_completed = false;
    conn->is_active = true;
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
    struct timespec sig_wait = {
        .tv_sec = MAX_INACTIVE_TIMEOUT,
        .tv_nsec = 0
    };
    time_t all_start, all_end;
    conn_t *conn, *tail_conn;

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
    while (1) {
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
                if (!p_conn_queue.active_conn_cnt &&
                        !p_conn_queue.inactive_conn_cnt) {
                    pr_debug("Srv: %s(): accept async, no connections "
                            "in queue, wait for new\n", __func__);
                    sigwait(&sig_set, &sig);
                } else if (!p_conn_queue.active_conn_cnt) {
                    pr_debug("Srv: %s(): accept async, no active "
                            "connections in queue, wait for timeout "
                            "and try to handle inactive connections "
                            "again\n", __func__);
                    sigtimedwait(&sig_set, NULL, &sig_wait);
                } else {
                    pr_debug("Srv: %s(): accept async, handle other "
                            "active connections in queue\n",
                            __func__);
                }
            } else {
                p_error("Srv: accept failed");
                exit(EXIT_FAILURE);
            }
        }

        tail_conn = p_conn_queue.tail;
        while (1) {
            conn = dequeue_conn(&p_conn_queue);
            if (!conn) {
                break;
            }

            if (ret = swap_to_conn_ctx(&p_conn_queue, conn)) {
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


        if (all_end - all_start >= 1) {
            printf("(%d) Srv: n_conn/s = %ld\n",
                    getpid(), atomic_load(&n_conn) - prev_n_conn);
            prev_n_conn = atomic_load(&n_conn);
            all_start = all_end;
        }
    }
}
