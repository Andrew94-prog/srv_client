#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include "srv_opts.h"
#include "srv_defs.h"

#define MAX_LINE_LEN 1024

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

static char *trim_spaces(char *s)
{
    int w = 0;
    char *c_s = s;

    while (*(c_s + w) != '\0' && *(c_s + w) != '\n') {
        if (isspace(*(c_s + w))) {
            w++;
        } else {
            *c_s = *(c_s + w);
            c_s++;
        }
    }

    *c_s = '\0';

    return s;
}

static int parse_srv_config_line(char *line)
{
    char *key, *val_str;
    int val;

    key = trim_spaces(line);

    if (*key == '\0' || *key == '\n' || *key == '#')
        return 0;

    val_str = strchr(key, ':');
    if (val_str == NULL)
        return 0;

    *val_str = '\0';
    val_str++;

    if (*val_str == '\0' || *val_str == '\n')
        return 0;

    if (strcmp(key, "port") == 0) {
        val = atoi(val_str);
        if (val == 0 || val < 1 || val > 65535) {
            fprintf(stderr, "Srv: invalid value %d for 'port' "
                            "in config file\n", val);
            return -EINVAL;
        }
        SRV_OPTS.port = val;
    } else if (strcmp(key, "num_workers") == 0) {
        val = atoi(val_str);
        if (val == 0 || val < 1 || val > MAX_NUM_WORKERS) {
            fprintf(stderr, "Srv: invalid value %d for 'num_workers' "
                            "in config file\n", val);
            return -EINVAL;
        }
        SRV_OPTS.num_workers = val;
    }

    return 0;
}

int parse_srv_config(void)
{
    FILE *config;
    char line[MAX_LINE_LEN];
    int ret = 0;

    config = fopen(SRV_CONFIG_FILE, "r");
    if (config == NULL) {
        return -ENOENT;
    }

    while (fgets(line, sizeof(line), config) != NULL) {
        ret = parse_srv_config_line(line);
        if (ret < 0)
            break;
    }

    fclose(config);

    return ret;
}