#include <stdio.h>
#include <stdlib.h>
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
    SRV_CONFIG.help = false;
}

int main(int argc, char *argv[])
{
    int srv_sock, srv_port, n_w;
    int ret, i;

    init_srv_config_default();

    ret = parse_srv_config();
    if (ret < 0) {
        fprintf(stderr, "%s(): config file parsing failed\n", __func__);
        fprintf(stderr, "%s", HELP_MSG);
    }

    ret = parse_srv_cmdline_opts(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "%s(): parse cmdline opts failed\n", __func__);
        fprintf(stderr, "%s", HELP_MSG);
        exit(EXIT_FAILURE);
    }

    if (SRV_CONFIG.help) {
        printf("%s", HELP_MSG);
        return 0;
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