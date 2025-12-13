#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#include "srv_send_recv.h"
#include "srv_conn_ctxt.h"
#include "srv_defs.h"

static bool is_http_message_end(char *recv_buf, ssize_t n_recv)
{
    return n_recv >= 4 && recv_buf[n_recv - 1] == '\n' &&
           recv_buf[n_recv - 2] == '\r' &&
           recv_buf[n_recv - 3] == '\n' &&
           recv_buf[n_recv - 4] == '\r';
}

ssize_t recv_http_msg(char *recv_buf, ssize_t to_recv)
{
    ssize_t n_recv = 0, count;
    time_t start_t, end_t;
    bool completed;
    int ret;

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

    return n_recv;
}

ssize_t send_http_msg(const char *send_buf, ssize_t to_send)
{
    ssize_t n_send = 0, count;
    time_t start_t, end_t;
    bool completed;
    int ret;

    completed = false;
    start_t = time(NULL);
    while (to_send && !completed) {
        count = write(p_conn_queue.curr_conn->conn_sock,
                    (const char *) send_buf + n_send, to_send);
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

    return n_send;
}

