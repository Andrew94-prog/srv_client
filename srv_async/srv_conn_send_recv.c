#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "srv_conn_send_recv.h"
#include "srv_conn_ctxt.h"
#include "srv_config.h"
#include "srv_defs.h"

/*
 * Check if at least one termination sequence "\r\n\r\n" conained
 * in http message.
 */
static bool is_http_message_end(char *recv_buf, ssize_t prev_n_recv,
                                ssize_t n_recv)
{
    char term_seq[] = "\r\n\r\n";
    int i;

    /* Check if message has enough length */
    if (n_recv < 4)
        return false;

    /*
     * Check if message ends with termination sequence. It is very often
     * case when http headers received without body (GET, HEAD methods)
     */
    if (!strncmp(recv_buf + n_recv - 4, term_seq, 4))
        return true;

    /* Termination sequence on the boundary of current and previous chunk */
    for (i = 3; i >= 1; i--) {
        if (prev_n_recv >= i && !strncmp(recv_buf + prev_n_recv - i,
                term_seq, 4))
            return true;
    }

    /* Common case: search for termination sequence in current chunk */
    for (i = prev_n_recv; i <= n_recv - 4; i++) {
        if (!strncmp(recv_buf + i, term_seq, 4))
            return true;
    }

    return false;
}

ssize_t recv_from_curr_conn(char *buf, ssize_t to_recv,
                            time_t timeout, bool terminator)
{
    ssize_t n_recv = 0, prev_n_recv = 0, count;
    bool recv_term = false, recv_timeout = false;

    curr_conn_update_active();
    curr_conn_start_op();
    while (to_recv && !recv_term && !recv_timeout) {
        count = read(curr_conn_sock(), buf, to_recv);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_active_timeout()) {
                    LOG(LOG_INFO2, "recv async, set inactivate "
                            "conn_sock = %d, curr_conn = %p\n",
                            curr_conn_sock(), curr_conn());
                    curr_conn_set_inactive();
                }

                LOG(LOG_INFO2, "no data to recv from sock, switch to main ctxt"
                    " conn_sock = %d, curr_conn = %p\n",
                    curr_conn_sock(), curr_conn());

                swap_to_main_ctx();
                continue;
            } else {
                LOG(LOG_ERROR, "recv from client failed, close connection"
                       " and switch back to main_ctx");
                curr_conn_close();
                swap_to_main_ctx();
                /* Should never get here*/
                return -1;
            }
        } else if (count > 0) {
            n_recv += count;
            to_recv -= count;

            curr_conn_update_active();

            if (terminator)
                recv_term = is_http_message_end(buf, prev_n_recv, n_recv);

            if (timeout != TIMEOUT_INF)
                recv_timeout = curr_conn_op_timeout(timeout);

            prev_n_recv = n_recv;
        } else {
            LOG(LOG_ERROR, "recv 0 bytes from client, end\n");
            recv_term = true;
        }
    }

    return n_recv;
}

ssize_t send_to_curr_conn(const char *buf, ssize_t to_send,
                        time_t timeout)
{
    ssize_t n_send = 0, count;
    bool send_ended = false, send_timeout = false;

    curr_conn_update_active();
    curr_conn_start_op();
    while (to_send && !send_ended && !send_timeout) {
        count = write(curr_conn_sock(), buf, to_send);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_active_timeout()) {
                    LOG(LOG_INFO2, "send async, set inactive "
                            "conn_sock = %d, curr_conn = %p\n",
                            curr_conn_sock(), curr_conn());
                    curr_conn_set_inactive();
                }

                LOG(LOG_INFO2, "no data to send to sock, switch to main ctxt"
                    " conn_sock = %d, curr_conn = %p\n",
                    curr_conn_sock(), curr_conn());

                swap_to_main_ctx();
                continue;
            } else {
                LOG(LOG_ERROR, "write to client failed, close connection"
                       " and switch back to main_ctx");
                curr_conn_close();
                swap_to_main_ctx();
                /* Should never get here*/
                return -1;
            }
        } else if (count > 0) {
            n_send += count;
            to_send -= count;

            curr_conn_update_active();

            if (timeout != TIMEOUT_INF)
                send_timeout = curr_conn_op_timeout(timeout);
        } else {
            LOG(LOG_ERROR, "sent 0 bytes to client, end\n");
            send_ended = true;
        }
    }

    return n_send;
}