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

    conn_queue->conn_cnt++;
}

conn_t *dequeue_conn(conn_queue_t *conn_queue)
{
    conn_t *ret;

    if (!conn_queue->head) {
        return NULL;
    }

    ret = conn_queue->head;

    if (conn_queue->head == conn_queue->tail) {
        conn_queue->head = NULL;
        conn_queue->tail = NULL;
    } else {
        conn_queue->head = conn_queue->head->next;
    }

    ret->next = NULL;
    conn_queue->conn_cnt--; 

    return ret;
}

void init_conn_queue(conn_queue_t *conn_queue)
{
    conn_queue->head = NULL;
    conn_queue->tail = NULL;
    conn_queue->curr_conn = NULL;
    conn_queue->conn_cnt = 0;
}
