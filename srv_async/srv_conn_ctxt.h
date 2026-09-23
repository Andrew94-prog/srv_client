#ifndef SRV_CONN_QUEUE_H
#define SRV_CONN_QUEUE_H

#include <stdbool.h>
#include <ucontext.h>

#include "srv_qlist.h"

typedef struct Conn {
    ucontext_t conn_ctx;
    char *orig_ss_sp;
    int orig_ss_size;

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
