#ifndef SRV_CONN_QUEUE_H
#define SRV_CONN_QUEUE_H

#include <stdbool.h>
#include <ucontext.h>

#include "srv_qlist.h"

typedef struct Conn {
    ucontext_t conn_ctx;
    int conn_sock;
    bool is_completed;
    bool is_active;

    struct qlist_head qlist;
} conn_t;

typedef struct ConnQueue {
    struct qlist_head qconn_list;

    conn_t *curr_conn;
    ucontext_t main_ctx;

    int active_conn_cnt;
    int inactive_conn_cnt;
} conn_queue_t;

extern conn_queue_t p_conn_queue;

void add_conn_to_queue(conn_queue_t *conn_queue, conn_t *conn);
void remove_conn_from_queue(conn_queue_t *conn_queue, conn_t *conn);
void init_conn_queue(conn_queue_t *conn_queue);

void curr_conn_close(conn_queue_t *conn_queue);
void curr_conn_keep_active(conn_queue_t *conn_queue);
void curr_conn_keep_inactive(conn_queue_t *conn_queue);
int swap_to_main_ctx(conn_queue_t *conn_queue);
int swap_to_conn_ctx(conn_queue_t *conn_queue, conn_t *conn);
void free_closed_conn(conn_t *conn);
int create_new_conn(int conn_sock);

#endif /* SRV_CONN_QUEUE_H */
