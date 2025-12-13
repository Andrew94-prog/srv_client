#include <stdio.h>
#include <stdbool.h>
#include <ucontext.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include "srv_conn_ctxt.h"
#include "srv_routines.h"
#include "srv_defs.h"

conn_queue_t p_conn_queue;

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

void curr_conn_close(conn_queue_t *conn_queue)
{
    close(conn_queue->curr_conn->conn_sock);
    conn_queue->curr_conn->is_completed = true;
}

void curr_conn_keep_active(conn_queue_t *conn_queue)
{
    conn_queue->curr_conn->is_active = true;
}

void curr_conn_keep_inactive(conn_queue_t *conn_queue)
{
    conn_queue->curr_conn->is_active = false;
}

void free_closed_conn(conn_t *conn)
{
    free(conn->conn_ctx.uc_stack.ss_sp);
    free(conn);
}

int swap_to_main_ctx(conn_queue_t *conn_queue)
{
    return swapcontext(&conn_queue->curr_conn->conn_ctx,
                        &conn_queue->main_ctx);
}

int swap_to_conn_ctx(conn_queue_t *conn_queue, conn_t *conn)
{
    conn_queue->curr_conn = conn;

    return swapcontext(&conn_queue->main_ctx, &conn->conn_ctx);
}

int create_new_conn(int conn_sock)
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
    makecontext(&conn_ctx, handle_one_connection, 0);

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
