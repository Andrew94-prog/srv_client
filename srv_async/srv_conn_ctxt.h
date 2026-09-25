#ifndef SRV_CONN_QUEUE_H
#define SRV_CONN_QUEUE_H

#include <stdbool.h>
#include <ucontext.h>

#include "srv_qlist.h"

/*
 * Context mem buf layout                                             0x0..0
 *                                                                      |
 * ------------------------------- <- ctx_buf_p, recv_buf_p           ......
 * 8192 b (2 pages) buffer to reveive data from clients               ......
 *                                                                    ......
 * -------------------------------                                    ......
 * 4096 b (1 page) guard page (SIGSEGV if touch it)                   ......
 * ------------------------------- <- ss_sp (top of coroutine stack)  ......
 * 32768 b (8 pages)                                                  ......
 * coroutine stack (grows down)                                       ......
 *                                                                    ......
 * -------------------------------                                    ......
 *                                                                    0xf..f
 */

typedef struct Conn {
    ucontext_t conn_ctx;
    char *ctx_buf_p;
    ssize_t ctx_buf_size;
    char *recv_buf_p;
    ssize_t recv_buf_size;
    char *ss_sp;
    ssize_t ss_size;

    int conn_sock;
    bool is_completed;
    bool is_active;
    unsigned long last_active;

    struct qlist_head qlist;
} conn_t;

typedef struct ConnQueue {
    struct qlist_head qconn_list;

    conn_t *curr_conn;
    ucontext_t main_ctx;

    int active_conn_cnt;
    int inactive_conn_cnt;
} conn_queue_t;

typedef struct ConnCtxCache {
    struct qlist_head qconn_list;
    int cnt;
} conn_ctx_cache_t;


void add_conn_to_queue(conn_t *conn);
void remove_conn_from_queue(conn_t *conn);
void init_conn_queue(void);
void init_conn_ctx_cache(void);

void curr_conn_close(void);
void curr_conn_set_active(void);
void curr_conn_set_inactive(void);
bool curr_conn_timeout(unsigned long timeout);
void curr_conn_update_active(void);
int swap_to_main_ctx(void);
int swap_to_conn_ctx(conn_t *conn);
void free_closed_conn(conn_t *conn);
int create_new_conn(int conn_sock);

conn_queue_t *conn_queue(void);
conn_t *curr_conn(void);
int curr_conn_sock(void);

#endif /* SRV_CONN_QUEUE_H */
