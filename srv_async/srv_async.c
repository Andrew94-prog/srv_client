#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <wait.h>
#include <time.h>
#include <pthread.h>
#include <ucontext.h>
#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <limits.h>
#include <sys/resource.h>


//#define DBG

#ifdef DBG
#define pr_debug(args...) printf(args)
#else
#define pr_debug
#endif

#define BUF_SIZE                           2048
#define CLINET_MSG_SIZE                     512
#define STACK_SIZE                         8192
#define CONN_TIMEOUT                          5


typedef struct Conn {
    ucontext_t conn_ctx;
    int conn_sock;
    bool completed;

    struct Conn *next;
} conn_t;

typedef struct ConnQueue {
    conn_t *head;
    conn_t *tail;
    pthread_spinlock_t lock;

    conn_t *curr_conn;
    ucontext_t main_ctx;
    pthread_t th_id;

    atomic_ulong cnt;
    bool sleeping;
} conn_queue_t;

static void enqueue_conn(conn_queue_t *conn_queue, conn_t *conn)
{
    pthread_spin_lock(&conn_queue->lock);

    conn->next = NULL;

    if (!conn_queue->tail) {
        conn_queue->head = conn;
        conn_queue->tail = conn;
    } else {
        conn_queue->tail->next = conn;
        conn_queue->tail = conn;
    }

    atomic_fetch_add(&conn_queue->cnt, 1);

    pthread_spin_unlock(&conn_queue->lock);
}

static conn_t *dequeue_conn(conn_queue_t *conn_queue)
{
    conn_t *ret;

    pthread_spin_lock(&conn_queue->lock);

    if (!conn_queue->head) {
        pthread_spin_unlock(&conn_queue->lock);
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

    atomic_fetch_sub(&conn_queue->cnt, 1);

    pthread_spin_unlock(&conn_queue->lock);

    return ret;
}

static int init_conn_queue(conn_queue_t *conn_queue)
{
    conn_queue->head = NULL;
    conn_queue->tail = NULL;
    pthread_spin_init(&conn_queue->lock, 0);

    conn_queue->curr_conn = NULL;
    
    atomic_init(&conn_queue->cnt, 0);
    conn_queue->sleeping = false;

    return 0;
}

static conn_queue_t *conn_queues;
static int srv_fd;
static int n_th = 1;

atomic_ulong n_conn = 0;
unsigned long prev_n_conn = 0;


static int enable_async(int sock)
{
    if (fcntl(sock, F_SETFL, O_NONBLOCK)) {
        close(sock);
        perror("Srv: fcntl set nonblocking failed");
        return -1;
    }

    if (fcntl(sock, F_SETSIG, SIGIO)) {
        close(sock);
        perror("Srv: fcntl set sig failed");
        return -1;
    }

    if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_ASYNC)) {
        close(sock);
        perror("Srv: fcntl set async failed");
        return -1;
    }

    if (fcntl(sock, F_SETOWN, gettid())) {
        close(sock);
        perror("Srv: fcntl set own failed");
        return -1;
    }

    return 0;
}

static void conn_routine(int q_id)
{
    conn_queue_t *conn_queue = &conn_queues[q_id];
    char buf[BUF_SIZE] = {0};
    ssize_t to_read = CLINET_MSG_SIZE, to_write = CLINET_MSG_SIZE;
    ssize_t n_read = 0, n_write = 0, count;
    time_t time_start, time_end, time_diff;
    int ret;

    pr_debug("Srv: %s(): start conn_sock = %d, conn = %p\n", __func__,
            conn_queue->curr_conn->conn_sock, conn_queue->curr_conn);

    /* Turn new connection socket to non-blocking mode */
    if (enable_async(conn_queue->curr_conn->conn_sock)) {
        perror("Srv: enabling async for conn_sock failed");
        exit(EXIT_FAILURE);
    }

    /* Read data from the client */
    time_diff = 0;
    while (to_read) {
        time_start = time(NULL);

        count = read(conn_queue->curr_conn->conn_sock,
                    (char *) buf + n_read, to_read);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pr_debug("Srv: %s(): read async, switch to main_ctx, "
                        "conn_sock = %d, curr_conn = %p\n",
                        __func__, conn_queue->curr_conn->conn_sock,
                        conn_queue->curr_conn);

                ret = swapcontext(&conn_queue->curr_conn->conn_ctx,
                                &conn_queue->main_ctx);
                if (ret == 0) {
                    continue;
                } else {
                    perror("Srv: switch ctx from client read to main failed");
                    exit(EXIT_FAILURE);
                }
            } else {
                perror("Srv: read from client failed");
                exit(EXIT_FAILURE);
            }
        }

        n_read += count;
        to_read -= count;

        time_end = time(NULL);

        if (count == 0)
            time_diff += (time_end - time_start);

        if (time_diff > CONN_TIMEOUT) {
            perror("Srv: connection timed out in read in conn routine, "
                    "switch to main ctx");

            close(conn_queue->curr_conn->conn_sock);
            conn_queue->curr_conn->completed = true;
            swapcontext(&conn_queue->curr_conn->conn_ctx,
                        &conn_queue->main_ctx);
        }
    }
    pr_debug("Srv: %s(): received from client: %s\n", __func__, buf);

    /* Send a response to the client */
    time_diff = 0;
    while (to_write) {
        time_start = time(NULL);

        count = write(conn_queue->curr_conn->conn_sock,
                    (char *) buf + n_write, to_write);

        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pr_debug("Srv: %s(): write async, switch to main_ctx, "
                        "conn_sock = %d, curr_conn = %p\n",
                        __func__, conn_queue->curr_conn->conn_sock,
                        conn_queue->curr_conn);

                ret = swapcontext(&conn_queue->curr_conn->conn_ctx,
                                &conn_queue->main_ctx);
                if (ret == 0) {
                    continue;
                } else {
                    perror("Srv: switch ctx from client write to main failed");
                    exit(EXIT_FAILURE);
                }
            } else {
                perror("Srv: write to client failed");
                exit(EXIT_FAILURE);
            }
        }

        n_write += count;
        to_write -= count;

        time_end = time(NULL);

        if (count == 0)
            time_diff += (time_end - time_start);

        if (time_diff > CONN_TIMEOUT) {
            perror("Srv: connection timed out in write in conn routine, "
                    "switch to main ctx");
            close(conn_queue->curr_conn->conn_sock);
            conn_queue->curr_conn->completed = true;
            swapcontext(&conn_queue->curr_conn->conn_ctx,
                        &conn_queue->main_ctx);
        }
    }
    pr_debug("Srv: %s(): Hello message sent to client\n", __func__);


    /* Close the socket for this client */
    close(conn_queue->curr_conn->conn_sock);
    conn_queue->curr_conn->completed = true;

    pr_debug("Srv: %s(): end conn_sock = %d, conn = %p\n", __func__,
            conn_queue->curr_conn->conn_sock, conn_queue->curr_conn);

    atomic_fetch_add(&n_conn, 1);

    swapcontext(&conn_queue->curr_conn->conn_ctx, &conn_queue->main_ctx);
}

static int create_new_conn(int conn_sock)
{
    char *conn_stack;
    ucontext_t conn_ctx;
    conn_t *conn;
    int i, min_q_id;
    unsigned long min_cnt = ULONG_MAX;

    pr_debug("Srv: %s(): new connection conn_sock = %d\n",
                __func__, conn_sock);

    for (i = 0; i < n_th; i++) {
        if (atomic_load(&conn_queues[i].cnt) < min_cnt) {
            min_cnt = atomic_load(&conn_queues[i].cnt);
            min_q_id = i;
        }
    }

    /* Create context with new stack for new connection  */
    if ((conn_stack = malloc(STACK_SIZE)) == NULL) {
        close(conn_sock);
        perror("Srv: failed to allocate stack for new connection\n");
        return -1;
    }

    getcontext(&conn_ctx);
    conn_ctx.uc_stack.ss_sp = conn_stack;
    conn_ctx.uc_stack.ss_size = STACK_SIZE;
    conn_ctx.uc_link = &conn_queues[min_q_id].main_ctx;
    makecontext(&conn_ctx, conn_routine, 1, min_q_id);

    /* Enqueue new connection */
    conn = malloc(sizeof(conn_t));
    if (!conn) {
        close(conn_sock);
        perror("Srv: allocation on ctx for new conn failed\n");
        return -1;
    }

    memcpy(&conn->conn_ctx, &conn_ctx, sizeof(ucontext_t));
    conn->conn_sock = conn_sock;
    conn->completed = false;

    enqueue_conn(&conn_queues[min_q_id], conn);

    if (conn_queues[min_q_id].sleeping) {
        pr_debug("Srv: %s(): wake up q id %d th_id %ld\n",
                __func__, min_q_id, conn_queues[min_q_id].th_id);
        pthread_kill(conn_queues[min_q_id].th_id, SIGUSR1);
    }

    pr_debug("Srv: %s(): created new connection conn_sock = %d, "
            "conn = %p to q id %d\n", __func__, conn_sock, conn, min_q_id);

    return 0;
}

static void sigusr1_hand(int sig, siginfo_t *si, void *ctx)
{
    pr_debug("Wake up signal got by queue thread\n");
}

static void *conn_thread_func(void *arg)
{
    int q_id = (int) arg;
    conn_queue_t *conn_queue = &conn_queues[q_id];
    conn_t *conn;
    int conn_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    time_t try_start, try_end, try_diff;

    while (1) {
        try_diff = 0;

        while (1) {
            try_start = time(NULL);

            conn = dequeue_conn(conn_queue);
            if (conn) {
                pr_debug("Srv: %s(): q_id %d fetch connection %p\n",
                        __func__, q_id, conn);
                break;
            } else {
                try_end = time(NULL);
                try_diff += (try_end - try_start);

                if (try_diff > CONN_TIMEOUT) {
                    pr_debug("Srv: %s(): q_id %d sleeping in cycle awiting for connection\n",
                            __func__, q_id);
                    conn_queue->sleeping = true;
                    sleep(1000);
                    conn_queue->sleeping = false;
                    try_diff = 0;
                }
            }
        }

        pr_debug("Srv: %s(): q_id %d switch to conn_sock = %d conn = %p\n",
                    __func__, q_id, conn->conn_sock, conn);

        conn_queue->curr_conn = conn;
        if (swapcontext(&conn_queue->main_ctx, &conn->conn_ctx) != 0) {
            perror("Srv: swapcontext to conn handler failed in cycle");
            exit(EXIT_FAILURE);
        }

        if (!conn->completed) {
            pr_debug("Srv: %s(): q_id %d conn_sock = %d conn = %p not completed\n",
                    __func__, q_id, conn->conn_sock, conn);
            enqueue_conn(conn_queue, conn);
        } else {
            pr_debug("Srv: %s(): q_id %d conn_sock = %d conn = %p completed\n",
                    __func__, q_id, conn->conn_sock, conn);
            free(conn->conn_ctx.uc_stack.ss_sp);
            free(conn);
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int conn_sock, port = 8080;
    struct timespec ts, te;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    struct sigaction sa;
    time_t all_start, all_end, try_start, try_end, try_time;
    struct timespec accept_start, accept_end, handle_start, handle_end;
    unsigned long accept_time, handle_time;
    conn_t *conn;
    int opt = 1, ret, i;

    sa.sa_handler = SIG_IGN;
    sigaction(SIGIO, &sa, NULL);

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigusr1_hand;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    if (argc >= 3) {
        n_th = atoi(argv[1]);
        port = atoi(argv[2]);
    } else if (argc >= 2) {
        n_th = atoi(argv[1]);
    }


    /* Create socket file descriptor */
    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd == -1) {
        perror("Srv: socket failed");
        exit(EXIT_FAILURE);
    }

    /* Forcefully attaching socket to the port */
    if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Srv: setsockopt failed for srv_fd");
        exit(EXIT_FAILURE);
    }

    /* Define the type of socket created */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    /* Bind the socket to the address and port */
    if (bind(srv_fd, (struct sockaddr *)&address, sizeof(address))) {
        perror("Srv: bind failed for srv_fd");
        exit(EXIT_FAILURE);
    }

    /* Allocate array of queues for all connection handling threads */
    conn_queues = malloc(sizeof(conn_queue_t) * n_th);
    if (!conn_queues) {
        perror("Srv: conn_queues allocation failed");
        exit(EXIT_FAILURE);
    }

    /* Init all connection queues */
    for (i = 0; i < n_th; i++) {
        if (init_conn_queue(&conn_queues[i])) {
            free(conn_queues);
            perror("Srv: init conn queue failed");
            exit(EXIT_FAILURE);
        }
    }

    /* Create thread for handling of connections */
    for (i = 0; i < n_th; i++) {
        pthread_t pth;

        pthread_create(&pth, NULL, &conn_thread_func, (void *) i);
        conn_queues[i].th_id = pth;
    }

    /* Start listening for incoming connections */
    if (listen(srv_fd, 1000) < 0) {
        perror("Srv: listen failed for srv_fd");
        exit(EXIT_FAILURE);
    }

    pr_debug("Srv: %s(): listening on port = %d srv_fd = %d\n",
            __func__, port, srv_fd);

    all_start = time(NULL);

    /* Accept incoming connections in a loop */
    try_time = 0;
    accept_time = 0; 
    while (1) {
        try_start = time(NULL);

        /* Accept new connection and create new socket for it */
        clock_gettime(CLOCK_REALTIME, &accept_start);
        conn_sock = accept(srv_fd, (struct sockaddr *)&address,
                           (socklen_t *)&addrlen);
        clock_gettime(CLOCK_REALTIME, &accept_end); 
        accept_time += ((accept_end.tv_sec * 1000000000 + accept_end.tv_nsec) -
                    (accept_start.tv_sec * 1000000000 + accept_start.tv_nsec));

        if (conn_sock >= 0) {
            pr_debug("Srv: %s(): accepted new conn_sock = %d in cycle\n",
                     __func__, conn_sock);

            clock_gettime(CLOCK_REALTIME, &handle_start);
            if (create_new_conn(conn_sock)) {
                perror("Srv: create new conn failed in cycle");
                exit(EXIT_FAILURE);
            }
            clock_gettime(CLOCK_REALTIME, &handle_end);
            handle_time += ((handle_end.tv_sec * 1000000000 + handle_end.tv_nsec) -
                    (handle_start.tv_sec * 1000000000 + handle_start.tv_nsec));
        } else {
            try_end = time(NULL);
            try_time += (try_end - try_start);

            if (try_time > CONN_TIMEOUT) {
                pr_debug("Srv: %s(): sleeping in cycle awiting accept\n",
                        __func__);
                sleep(1000);
                try_time = 0;
            }
        }

        all_end = time(NULL);

        if (all_end - all_start >= 1) {
            printf("Srv: n_conn/s = %ld, accept_time = %ld ms, "
                    "handle_time = %ld ms\n",
                    atomic_load(&n_conn) - prev_n_conn,
                    accept_time / 1000000,
                    handle_time / 1000000);
            prev_n_conn = atomic_load(&n_conn);
            all_start = all_end;
            accept_time = 0;
            handle_time = 0;
        }
    }

    return 0;
}