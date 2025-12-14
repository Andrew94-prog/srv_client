#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include "srv_opts.h"
#include "srv_defs.h"

srv_opts_t SRV_OPTS = {
    .port = DEFAULT_SRV_PORT,
    .num_workers = DEFAULT_NUM_WORKERS,
    .help = false
};

const char HELP_MSG[] = "Supported options for server:\n"
                  "-w NUM_WORKERS - number of worker processes "
                  "to create, should be from 1 to 1000\n"
                  "-p PORT_NUM - port number to listen on server, "
                  "should be from 1 to 65536 (max valid port)\n"
                  "-h - get this help\n";

int parse_srv_opts(int argc, char *argv[])
{
    int c, ret = 0;
    long val;
    char *endptr;

    while ((c = getopt(argc, argv, "w:p:h")) != -1) {
        switch (c) {
        case 'w': {
            endptr = NULL;
            val = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || errno == ERANGE ||
                        val < 1 || val > MAX_NUM_WORKERS) {
                fprintf(stderr, "Srv: invalid value %ld for -w "
                                "option\n", val);
                ret = -EINVAL;
                break;
            }
            SRV_OPTS.num_workers = (int) val;
            break;
        }
        case 'p': {
            endptr = NULL;
            val = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || errno == ERANGE ||
                        val < 1 || val > 65535) {
                fprintf(stderr, "Srv: invalid value %ld for -p "
                                "option\n", val);
                ret = -EINVAL;
                break;
            }
            SRV_OPTS.port = (int) val;
            break;
        }
        case 'h':
            SRV_OPTS.help = true;
            break;
        default:
            SRV_OPTS.help = true;
            ret = -EINVAL;
        }
    }

    return ret;
}
