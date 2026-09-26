#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/mman.h>
#include <time.h>

#include "srv_conn_ctxt.h"
#include "srv_routines.h"
#include "srv_config.h"
#include "srv_defs.h"
#include "srv_qlist.h"

static conn_queue_t p_conn_queue;
static conn_ctx_cache_t p_conn_ctx_cache;

static unsigned long curr_time(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* -------------------------------------------------------------- */

static conn_t *alloc_conn_ctx_mem(void)
{
    char *ctx_buf;
    conn_t *conn;

    /* Memory for receive buffer, guard page, coroutine stack */
    ctx_buf = (char *) mmap(NULL, CTX_BUF_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ctx_buf == MAP_FAILED) {
        LOG(LOG_ERROR, "failed to allocate stack for connection\n");
        return NULL;
    }

    if (mprotect(ctx_buf + RECV_BUF_SIZE, GUARD_SIZE, PROT_NONE)) {
        LOG(LOG_ERROR, "failed to protect guard page for conn ctx\n");
        munmap(ctx_buf, CTX_BUF_SIZE);
        return NULL;
    }

    conn = (conn_t *) malloc(sizeof(conn_t));
    if (!conn) {
        LOG(LOG_ERROR, "allocation on ctx for connection failed\n");
        munmap(ctx_buf, CTX_BUF_SIZE);
        return NULL;
    }

    conn->ctx_buf_p = ctx_buf;
    conn->ctx_buf_size = CTX_BUF_SIZE;
    conn->recv_buf_p = ctx_buf;
    conn->recv_buf_size = RECV_BUF_SIZE;
    conn->ss_sp = ctx_buf + RECV_BUF_SIZE + GUARD_SIZE;
    conn->ss_size = STACK_SIZE;

    return conn;
}

static void free_conn_ctx_mem(conn_t *conn)
{
    munmap(conn->ctx_buf_p, conn->ctx_buf_size);
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
        LOG(LOG_ERROR, "conn ctx cache was not properly filled\n");
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
    conn->conn_ctx.uc_stack.ss_sp = conn->ss_sp;
    conn->conn_ctx.uc_stack.ss_size = conn->ss_size;
    conn->conn_ctx.uc_link = &p_conn_queue.main_ctx;
    makecontext(&conn->conn_ctx, handle_one_connection, 0);

    conn->conn_sock = conn_sock;
    conn->is_completed = false;
    conn->is_active = true;
}

/* --------------------------------------------------------------- */

void add_conn_to_queue(conn_t *conn)
{
    qlist_add_head(&p_conn_queue.qconn_list, &conn->qlist);

    if (conn->is_active) {
        p_conn_queue.active_conn_cnt++;
    } else {
        p_conn_queue.inactive_conn_cnt++;
    }
}

void remove_conn_from_queue(conn_t *conn)
{
    qlist_del_entry(&conn->qlist);

    if (conn->is_active) {
        p_conn_queue.active_conn_cnt--;
    } else {
        p_conn_queue.inactive_conn_cnt--;
    }
}

void init_conn_queue(void)
{
    qlist_head_init(&p_conn_queue.qconn_list);

    p_conn_queue.curr_conn = NULL;
    p_conn_queue.active_conn_cnt = 0;
    p_conn_queue.inactive_conn_cnt = 0;
}

void init_conn_ctx_cache(void)
{
    qlist_head_init(&p_conn_ctx_cache.qconn_list);
    p_conn_ctx_cache.cnt = 0;

    fill_conn_ctx_cache();
}

void curr_conn_close(void)
{
    if (!p_conn_queue.curr_conn->is_completed) {
        close(p_conn_queue.curr_conn->conn_sock);
        p_conn_queue.curr_conn->is_completed = true;
    }
}

void curr_conn_set_active(void)
{
    if (!p_conn_queue.curr_conn->is_active) {
        p_conn_queue.curr_conn->is_active = true;
        p_conn_queue.curr_conn->last_active = curr_time();
        p_conn_queue.active_conn_cnt++;
        p_conn_queue.inactive_conn_cnt--;
    }
}

void curr_conn_set_inactive(void)
{
    if (p_conn_queue.curr_conn->is_active) {
        p_conn_queue.curr_conn->is_active = false;
        p_conn_queue.active_conn_cnt--;
        p_conn_queue.inactive_conn_cnt++;
    }
}

void curr_conn_update_active(void)
{
    p_conn_queue.curr_conn->last_active = curr_time();
}

bool curr_conn_active_timeout(void)
{
    return (curr_time() - p_conn_queue.curr_conn->last_active >
            MAX_ACTIVE_TIMEOUT) ? true : false;
}

void free_closed_conn(conn_t *conn)
{
    free_conn_ctx(conn);
}

int swap_to_main_ctx(void)
{
    conn_t *conn = p_conn_queue.curr_conn;

    if (swapcontext(&conn->conn_ctx, &p_conn_queue.main_ctx)) {
        LOG(LOG_ERROR, "switch to main_ctx failed with unknown error"
            " from: conn_sock = %d, conn = %p\n", conn->conn_sock, conn);
        exit(EXIT_FAILURE);
    }

    return 0;
}

int swap_to_conn_ctx(conn_t *conn)
{
    p_conn_queue.curr_conn = conn;

    if (swapcontext(&p_conn_queue.main_ctx, &conn->conn_ctx)) {
        LOG(LOG_ERROR, "switch to conn_ctx failed with unknown error"
            " conn_sock = %d, conn = %p\n", conn->conn_sock, conn);
        exit(EXIT_FAILURE);
    }

    return 0;
}

int create_new_conn(int conn_sock)
{
    conn_t *conn;

    LOG(LOG_INFO1, "new connection conn_sock = %d\n", conn_sock);

    conn = alloc_conn_ctx();
    if (!conn) {
        LOG(LOG_ERROR, "allocation of new conn ctx failed\n");
        return -1;
    }
    init_new_conn_ctx(conn, conn_sock);
    add_conn_to_queue(conn);

    LOG(LOG_INFO1, "created new connection conn_sock = %d,"
            " conn = %p\n", conn_sock, conn);

    return 0;
}

conn_queue_t *conn_queue(void)
{
    return &p_conn_queue;
}

conn_t *curr_conn(void)
{
    return p_conn_queue.curr_conn;
}

int curr_conn_sock(void)
{
    return p_conn_queue.curr_conn->conn_sock;
}