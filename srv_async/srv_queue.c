#include <stddef.h>

#include "srv_conn_queue.h"

void enqueue_conn(conn_queue_t *conn_queue, conn_t *conn)
{
    conn->next = NULL;

    if (!conn_queue->tail) {
        conn_queue->head = conn;
        conn_queue->tail = conn;
    } else {
        conn_queue->tail->next = conn;
        conn_queue->tail = conn;
    }

    if (conn->is_active) {
        conn_queue->active_conn_cnt++;
    } else {
        conn_queue->inactive_conn_cnt++;
    }
}

conn_t *dequeue_conn(conn_queue_t *conn_queue)
{
    conn_t *conn;

    if (!conn_queue->head) {
        return NULL;
    }

    conn = conn_queue->head;

    if (conn_queue->head == conn_queue->tail) {
        conn_queue->head = NULL;
        conn_queue->tail = NULL;
    } else {
        conn_queue->head = conn_queue->head->next;
    }

    conn->next = NULL;

    if (conn->is_active) {
        conn_queue->active_conn_cnt--;
    } else {
        conn_queue->inactive_conn_cnt--;
    }

    return conn;
}

void init_conn_queue(conn_queue_t *conn_queue)
{
    conn_queue->head = NULL;
    conn_queue->tail = NULL;
    conn_queue->curr_conn = NULL;
    conn_queue->active_conn_cnt = 0;
    conn_queue->inactive_conn_cnt = 0;
}
