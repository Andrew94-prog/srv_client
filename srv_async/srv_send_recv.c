#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#include "srv_send_recv.h"
#include "srv_conn_ctxt.h"
#include "srv_config.h"
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
    bool completed;
    int ret;

    completed = false;
    curr_conn_update_active(&p_conn_queue);
    while (to_recv && !completed) {
        count = read(p_conn_queue.curr_conn->conn_sock,
                        (char *) recv_buf + n_recv, to_recv);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_timeout(&p_conn_queue,
                                      MAX_ACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "recv async, keep inactivate "
                            "conn_sock = %d, curr_conn = %p\n",
                            p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_set_inactive(&p_conn_queue);
                }

                if (curr_conn_timeout(&p_conn_queue,
                                      MAX_INACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "recv async, close "
                            "conn_sock = %d, curr_conn = %p\n",
                            p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_close(&p_conn_queue);
                }


                ret = swap_to_main_ctx(&p_conn_queue);
                if (ret == 0) {
                    continue;
                } else {
                    LOG(LOG_ERROR, "switch ctx from client recv to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                LOG(LOG_ERROR, "recv from client failed, close connection"
                       " and switch back to main_ctx");
                curr_conn_close(&p_conn_queue);
                swap_to_main_ctx(&p_conn_queue);
            }
        } else if (count > 0) {
            n_recv += count;
            to_recv -= count;

            curr_conn_update_active(&p_conn_queue);
            completed = is_http_message_end(recv_buf, n_recv);
        } else {
            LOG(LOG_ERROR, "recv 0 bytes from client, end\n");
            completed = true;
        }
    }

    return n_recv;
}

ssize_t send_http_msg(const char *send_buf, ssize_t to_send)
{
    ssize_t n_send = 0, count;
    bool completed;
    int ret;

    completed = false;
    curr_conn_update_active(&p_conn_queue);
    while (to_send && !completed) {
        count = write(p_conn_queue.curr_conn->conn_sock,
                    (const char *) send_buf + n_send, to_send);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_timeout(&p_conn_queue,
                                      MAX_ACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "send async, keep inactive "
                            "conn_sock = %d, curr_conn = %p\n",
                            p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_set_inactive(&p_conn_queue);
                }

                if (curr_conn_timeout(&p_conn_queue,
                                      MAX_INACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "send async, close "
                            "conn_sock = %d, curr_conn = %p\n",
                            p_conn_queue.curr_conn->conn_sock,
                            p_conn_queue.curr_conn);
                    curr_conn_close(&p_conn_queue);
                }


                ret = swap_to_main_ctx(&p_conn_queue);
                if (ret == 0) {
                    continue;
                } else {
                    LOG(LOG_ERROR, "switch ctx from client send to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                LOG(LOG_ERROR, "write to client failed");
                exit(EXIT_FAILURE);
            }
        } else if (count > 0) {
            n_send += count;
            to_send -= count;
            curr_conn_update_active(&p_conn_queue);
        } else {
            LOG(LOG_INFO2, "sent 0 bytes to client, end\n");
            completed = true;
        }
    }

    return n_send;
}

