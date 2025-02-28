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
#include <stdatomic.h>


//#define DBG

#ifdef DBG
#define pr_debug(str) printf(str)
#else
#define pr_debug
#endif

#define BUF_SIZE                           1024
#define CLINET_MSG_SIZE                     512
#define STACK_SIZE                         8192
#define CONN_TIMEOUT                          5

atomic_ulong n_conn = 0;
unsigned long prev_n_conn = 0;

static void sigchld_hand(int sig, siginfo_t *si, void *ctx)
{
    int status;

    while (waitpid(-1, &status, WNOHANG) > 0) {};
}

static void conn_routine(int new_socket)
{
    char buf[BUF_SIZE] = {0};
    ssize_t to_read = CLINET_MSG_SIZE, to_write = CLINET_MSG_SIZE;
    ssize_t n_read = 0, n_write = 0, count;

    /* Read data from the client */
    while (to_read) {
        count = read(new_socket, (char *) buf + n_read, to_read);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Srv: read from client failed");
                exit(EXIT_FAILURE);
            }
        }

        n_read += count;
        to_read -= count;
    }
    pr_debug("Srv: received fron client: %s\n", buf);

    /* Send a response to the client */
    while (to_write) {
        count = write(new_socket, (char *) buf + n_write, to_write);

        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Srv: write to client failed");
                exit(EXIT_FAILURE);
            }
        }

        n_write += count;
        to_write -= count;
    }
    pr_debug("Srv: Hello message sent to client\n");

    /* Close the socket for this client */
    close(new_socket);

    atomic_fetch_add(&n_conn, 1);
}


static void *thread_routine(void *arg)
{
    conn_routine((int) arg);

    return NULL;
}

static void thread_new_conn(int new_socket)
{
    int ret;
    pthread_t th;

    do {
        ret = pthread_create(&th, NULL, thread_routine, (void *) new_socket);

        if (ret) {
            if (errno == EAGAIN || errno == EINTR || errno == ECHILD) {
                continue;
            } else {
                perror("Srv: new thread for new conn failed");
                exit(EXIT_FAILURE);
            }
        } else {
            pthread_detach(th);
            break;
        }
    } while (true);
}

int main(int argc, char *argv[])
{
    int server_fd, new_socket, port = 8080;
    struct timespec ts, te;
    unsigned long accept_time = 0, handle_time = 0;
    struct sockaddr_in address;
    struct sigaction sa;
    time_t all_start, all_end;
    int opt = 1;
    int addrlen = sizeof(address);

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigchld_hand;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    /* Create socket file descriptor */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Srv: socket failed");
        exit(EXIT_FAILURE);
    }

    /* Forcefully attaching socket to the port */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Srv: setsockopt failed");
        exit(EXIT_FAILURE);
    }

    /* Define the type of socket created */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    /* Bind the socket to the address and port */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))) {
        perror("Srv: bind failed");
        exit(EXIT_FAILURE);
    }

    /* Start listening for incoming connections */
    if (listen(server_fd, 1000) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    pr_debug("Srv: listening on port %d\n", port);

    all_start = time(NULL);

    /* Accept incoming connections in a loop */
    while (1) {
        clock_gettime(CLOCK_REALTIME, &ts);
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                (socklen_t*)&addrlen)) < 0) {
            if (errno == EAGAIN || errno == EINTR || errno == ECHILD)
                continue;
            perror("Srv: accept failed");
            exit(EXIT_FAILURE);
        }
        clock_gettime(CLOCK_REALTIME, &te);
        accept_time += (te.tv_sec * 1000000000 + te.tv_nsec) -
                        (ts.tv_sec * 1000000000 + ts.tv_nsec);



        clock_gettime(CLOCK_REALTIME, &ts);
        thread_new_conn(new_socket);
        clock_gettime(CLOCK_REALTIME, &te);
        handle_time += (te.tv_sec * 1000000000 + te.tv_nsec) -
                        (ts.tv_sec * 1000000000 + ts.tv_nsec);

        all_end = time(NULL);

        if (all_end - all_start >= 1) {
            if (atomic_load(&n_conn) - prev_n_conn)
                printf("Srv: n_conn/s = %ld, accept_time = %ld ms, "
                    "handle_time = %ld ms\n",
                    atomic_load(&n_conn) - prev_n_conn,
                    accept_time / 1000000,
                    handle_time / 1000000);
            accept_time = 0;
            handle_time = 0;
            prev_n_conn = atomic_load(&n_conn);
            all_start = all_end;
        }
    }

    return 0;
}