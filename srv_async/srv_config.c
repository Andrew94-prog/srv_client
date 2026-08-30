#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include "srv_config.h"
#include "srv_defs.h"

srv_config_t SRV_CONFIG;

const char HELP_MSG[] = "Supported cmdline options for server:\n"
                  "-w ('num_workers') NUM_WORKERS - number of worker processes "
                  "to create, should be from 1 to 1000\n"
                  "-p ('port') PORT_NUM - port number to listen on server, "
                  "should be from 1 to 65536 (max valid port)\n"
                  "-f ('log_file') path_to_log_file - file for reporting errors "
                  "and info\n"
                  "-l ('log_level') log_level - verbosity of info prints in log\n"
                  "-h - get this help\n";

static int parse_range_value(const char *val_str, int *val, int min, int max,
                            const char *p_name, bool is_config)
{
    int tmp;

    tmp = atoi(val_str);
    if (tmp < min || tmp > max) {
        fprintf(stderr, "%s(): invalid value %d for '%s' "
                "in %s, should be in range [%d-%d]\n",
                __func__, tmp, p_name,
                is_config ? SRV_CONFIG_FILE : "cmdline",
                min, max);
        return -ERANGE;
    }

    *val = tmp;

    return 0;
}

static int parse_log_file_name(const char *val_str, bool is_config)
{
    SRV_CONFIG.log_file_name = malloc(strlen(val_str) + 1);
    if (!SRV_CONFIG.log_file_name) {
        fprintf(stderr, "%s(): could not allocate %ld bytes for "
                "log_file name %s in %s\n", __func__,
                strlen(val_str) + 1, val_str,
                is_config ? SRV_CONFIG_FILE : "cmdline");
        return -ENOMEM;
    }

    strcpy(SRV_CONFIG.log_file_name, val_str);
    SRV_CONFIG.log_file_desc = fopen(val_str, "w");

    if (!SRV_CONFIG.log_file_desc) {
        fprintf(stderr, "%s(): could not open 'log_file'=%s "
                "from %s: %s\n", __func__, val_str,
                is_config ? SRV_CONFIG_FILE : "cmdline",
                strerror(errno));
        return -errno;
    }

    return 0;
}

int parse_srv_cmdline_opts(int argc, char *argv[])
{
    int c, val, ret;

    while ((c = getopt(argc, argv, "w:p:f:l:h")) != -1) {
        switch (c) {
        case 'w':
            ret = parse_range_value(optarg, &val, 1, MAX_NUM_WORKERS,
                                    "num_workers", false);
            if (ret)
                return ret;

            SRV_CONFIG.num_workers = val;
            break;
        case 'p':
            ret = parse_range_value(optarg, &val, 1, 65535, "port", false);
            if (ret)
                return ret;

            SRV_CONFIG.port = val;
            break;
        case 'f':
            ret = parse_log_file_name(optarg, false);
            if (ret)
                return ret;
            break;
        case 'l':
            ret = parse_range_value(optarg, &val, LOG_ERROR, LOG_INFO2,
                                    "log_level", false);
            if (ret)
                return ret;

            SRV_CONFIG.log_level = val;
            break;
        case 'h':
            SRV_CONFIG.help = true;
            break;
        default:
            SRV_CONFIG.help = true;
            return -EINVAL;
        }
    }

    return 0;
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
    int val, ret;

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
        ret = parse_range_value(val_str, &val, 1, 65535,
                                "port", true);
        if (ret)
            return ret;

        SRV_CONFIG.port = val;
    } else if (strcmp(key, "num_workers") == 0) {
        ret = parse_range_value(val_str, &val, 1, MAX_NUM_WORKERS,
                                "num_workers", true);
        if (ret)
            return ret;

        SRV_CONFIG.num_workers = val;
    } else if (strcmp(key, "log_file") == 0) {
        ret = parse_log_file_name(val_str, true);
        if (ret)
            return ret;
    } else if (strcmp(key, "log_level") == 0) {
        ret = parse_range_value(val_str, &val, LOG_ERROR, LOG_INFO2,
                                "log_level", true);
        if (ret)
            return ret;

        SRV_CONFIG.log_level = val;
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