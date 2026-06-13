#include <stdio.h>
#include <stdbool.h>
#include <ucontext.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/mman.h>

#include "srv_conn_ctxt.h"
#include "srv_routines.h"
#include "srv_defs.h"
#include "srv_qlist.h"

conn_queue_t p_conn_queue;
conn_ctx_cache_t p_conn_ctx_cache;

static conn_t *alloc_conn_ctx_mem(void)
{
    char *conn_stack;
    conn_t *conn;

    /* Create context with new stack for new connection  */
    conn_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (conn_stack == MAP_FAILED) {
        p_error("Srv: failed to allocate stack for connection\n");
        return NULL;
    }

    /* Enqueue new connection with context */
    conn = malloc(sizeof(conn_t));
    if (!conn) {
        p_error("Srv: allocation on ctx for connection failed\n");
        return NULL;
    }

    conn->orig_ss_sp = conn_stack;
    conn->orig_ss_size = STACK_SIZE;

    return conn;
}

static void free_conn_ctx_mem(conn_t *conn)
{
    munmap(conn->orig_ss_sp, conn->orig_ss_size);
    free(conn);
}

static void fill_conn_ctx_cache(void)
{
    conn_t *conn;
    int cnt = MIN_CONN_CTX_CACHE_CNT - p_conn_ctx_cache.cnt;

    while (cnt) {
        conn = alloc_conn_ctx_mem();
        if (conn) {
            qlist_add_head(&p_conn_ctx_cache.qconn_list, &conn->qlist);
            p_conn_ctx_cache.cnt++;
        }
        cnt--;
    }

    if (p_conn_ctx_cache.cnt < MIN_CONN_CTX_CACHE_CNT)
        p_error("Srv: conn ctx cache was not properly filled\n");
}

static conn_t *alloc_conn_ctx(void)
{
    conn_t *conn;

    if (!p_conn_ctx_cache.cnt)
        fill_conn_ctx_cache();

    if (!p_conn_ctx_cache.cnt)
        return NULL;

    conn = qlist_first_entry(conn_t, qlist, &p_conn_ctx_cache.qconn_list);
    qlist_del_entry(&conn->qlist);
    p_conn_ctx_cache.cnt--;

    return conn;
}

static void free_conn_ctx(conn_t *conn)
{
    if (p_conn_ctx_cache.cnt < MAX_CONN_CTX_CACHE_CNT) {
        qlist_add_head(&p_conn_ctx_cache.qconn_list, &conn->qlist);
        p_conn_ctx_cache.cnt++;
    } else {
        free_conn_ctx_mem(conn);
    }
}

static void init_new_conn_ctx(conn_t *conn, int conn_sock)
{
    getcontext(&conn->conn_ctx);
    conn->conn_ctx.uc_stack.ss_sp = conn->orig_ss_sp;
    conn->conn_ctx.uc_stack.ss_size = conn->orig_ss_size;
    conn->conn_ctx.uc_link = &p_conn_queue.main_ctx;
    makecontext(&conn->conn_ctx, handle_one_connection, 0);

    conn->conn_sock = conn_sock;
    conn->is_completed = false;
    conn->is_active = true;
}

/* --------------------------------------------------------------- */

void add_conn_to_queue(conn_queue_t *conn_queue, conn_t *conn)
{
    qlist_add_head(&conn_queue->qconn_list, &conn->qlist);

    if (conn->is_active) {
        conn_queue->active_conn_cnt++;
    } else {
        conn_queue->inactive_conn_cnt++;
    }
}

void remove_conn_from_queue(conn_queue_t *conn_queue, conn_t *conn)
{
    qlist_del_entry(&conn->qlist);

    if (conn->is_active) {
        conn_queue->active_conn_cnt--;
    } else {
        conn_queue->inactive_conn_cnt--;
    }
}

void init_conn_queue(conn_queue_t *conn_queue)
{
    qlist_head_init(&conn_queue->qconn_list);

    conn_queue->curr_conn = NULL;
    conn_queue->active_conn_cnt = 0;
    conn_queue->inactive_conn_cnt = 0;
}

void init_conn_ctx_cache(conn_ctx_cache_t *conn_ctx_cache)
{
    qlist_head_init(&conn_ctx_cache->qconn_list);
    conn_ctx_cache->cnt = 0;

    fill_conn_ctx_cache();
}

void curr_conn_close(conn_queue_t *conn_queue)
{
    close(conn_queue->curr_conn->conn_sock);
    conn_queue->curr_conn->is_completed = true;
}

void curr_conn_keep_active(conn_queue_t *conn_queue)
{
    if (!conn_queue->curr_conn->is_active) {
        conn_queue->curr_conn->is_active = true;
        conn_queue->active_conn_cnt++;
        conn_queue->inactive_conn_cnt--;
    }
}

void curr_conn_keep_inactive(conn_queue_t *conn_queue)
{
    if (conn_queue->curr_conn->is_active) {
        conn_queue->curr_conn->is_active = false;
        conn_queue->active_conn_cnt--;
        conn_queue->inactive_conn_cnt++;
    }
}

void free_closed_conn(conn_t *conn)
{
    free_conn_ctx(conn);
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
    conn_t *conn;

    pr_debug("Srv: %s(): new connection conn_sock = %d\n",
                __func__, conn_sock);

    conn = alloc_conn_ctx();
    if (!conn) {
        p_error("Srv: allocation of new conn ctx failed\n");
        return -1;
    }
    init_new_conn_ctx(conn, conn_sock);
    add_conn_to_queue(&p_conn_queue, conn);

    pr_debug("Srv: %s(): created new connection conn_sock = %d,"
            " conn = %p\n", __func__, conn_sock, conn);

    return 0;
}
