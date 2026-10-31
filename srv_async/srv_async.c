#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

#include "srv_defs.h"
#include "srv_routines.h"
#include "srv_sock.h"
#include "srv_config.h"

static void wait_for_workers(void)
{
    int ret, status;
    FILE *fout = SRV_CONFIG.log_file_desc;

    while ((ret = wait(&status)) > 0) {
        fprintf(fout, "%s(): %d worker %s %d\n", __func__, ret,
               WIFEXITED(status) ? "exited with code" :
                                "terminated by signal",
               WIFEXITED(status) ? WEXITSTATUS(status) :
                                WTERMSIG(status));
    }
}

static void close_srv_log(void)
{
    if (SRV_CONFIG.log_file_desc &&
            SRV_CONFIG.log_file_desc != stdout) {
        fclose(SRV_CONFIG.log_file_desc);
        SRV_CONFIG.log_file_desc = stdout;
    }
}

static void handle_sigint(int sig)
{
    wait_for_workers();
    close_srv_log();
}

static void print_srv_config(void)
{
    FILE *fout = SRV_CONFIG.log_file_desc;

    fprintf(fout, "Server started with parameters:\n");
    fprintf(fout, "port: %d\n", SRV_CONFIG.port);
    fprintf(fout, "num_workers: %d\n", SRV_CONFIG.num_workers);
    fprintf(fout, "log_file_name: %s\n", SRV_CONFIG.log_file_name ?
                SRV_CONFIG.log_file_name : "stdout");
    fprintf(fout, "log_file_desc: %p\n", SRV_CONFIG.log_file_desc);
    fprintf(fout, "base_dir: %s\n", SRV_CONFIG.base_dir ?
                SRV_CONFIG.base_dir : "(not set)");
    fprintf(fout, "host_name: %s\n", SRV_CONFIG.host_name ?
                SRV_CONFIG.host_name : "(not set)");
    fprintf(fout, "db_socket: %s\n", SRV_CONFIG.db_socket ?
                SRV_CONFIG.db_socket : "(not set)");
    fprintf(fout, "db_name: %s\n", SRV_CONFIG.db_name ?
                SRV_CONFIG.db_name : "(not set)");
    fprintf(fout, "db_user: %s\n", SRV_CONFIG.db_user ?
                SRV_CONFIG.db_user : "(not set)");
    fprintf(fout, "db_password: %s\n",
                SRV_CONFIG.db_password ? "****" : "(not set)");
    fprintf(fout, "\n");
    fflush(fout);
}

static void init_srv_config_default(void)
{
    SRV_CONFIG.port = DEFAULT_SRV_PORT;
    SRV_CONFIG.num_workers = DEFAULT_NUM_WORKERS;
    SRV_CONFIG.log_file_name = NULL;
    SRV_CONFIG.log_file_desc = stdout;
    SRV_CONFIG.log_level = 0;
    SRV_CONFIG.base_dir = strdup(DEFAULT_BASE_DIR);
    SRV_CONFIG.host_name = strdup(DEFAULT_HOST_NAME);
    SRV_CONFIG.db_socket = strdup(DEFAULT_DB_SOCKET);
    SRV_CONFIG.db_name = strdup(DEFAULT_DB_NAME);
    SRV_CONFIG.db_user = strdup(DEFAULT_DB_USER);
    SRV_CONFIG.db_password = strdup(DEFAULT_DB_PASSWORD);
}

int main(void)
{
    int srv_sock, srv_port, n_w;
    int ret, i;

    init_srv_config_default();

    ret = parse_srv_config();
    if (ret < 0) {
        fprintf(stderr, "%s(): could not parse config file %s\n",
                __func__, SRV_CONFIG_FILE);
        fprintf(stderr, HELP_MSG, MAX_NUM_WORKERS, LOG_INFO2);
        exit(EXIT_FAILURE);
    }

    /* Validate base_dir from config file or default one */
    if (validate_srv_base_dir(SRV_CONFIG.base_dir)) {
        fprintf(stderr, "%s(): invalid base_dir '%s'\n", __func__,
                SRV_CONFIG.base_dir);
        exit(EXIT_FAILURE);
    }

    print_srv_config();

    signal(SIGINT, handle_sigint);

    srv_port = SRV_CONFIG.port;
    n_w = SRV_CONFIG.num_workers;

    srv_sock = create_listening_socket(srv_port);
    if (srv_sock < 0) {
        LOG(LOG_ERROR, "creation of socket failed");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < n_w; i++) {
       if ((ret = fork()) == 0) {
           handle_connections_routine(srv_sock);
       }
    }

    wait_for_workers();
    close_srv_log();

    return 0;
}