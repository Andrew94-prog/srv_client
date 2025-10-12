#ifndef SRV_CONN_QUEUE_H
#define SRV_CONN_QUEUE_H

#include <stdbool.h>
#include <ucontext.h>

typedef struct Conn {
    ucontext_t conn_ctx;
    int conn_sock;
    bool completed;

    struct Conn *next;
} conn_t;

typedef struct ConnQueue {
    conn_t *head;
    conn_t *tail;

    conn_t *curr_conn;
    ucontext_t main_ctx;

    int conn_cnt;
} conn_queue_t;

void enqueue_conn(conn_queue_t *conn_queue, conn_t *conn);
conn_t *dequeue_conn(conn_queue_t *conn_queue);
void init_conn_queue(conn_queue_t *conn_queue);
int enable_async(int sock);
void process_conn_func(int srv_sock);

#endif /* SRV_CONN_QUEUE_H */
