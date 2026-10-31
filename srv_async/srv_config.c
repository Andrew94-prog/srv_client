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

static int parse_range_value(const char *val_str, int *val, int min, int max,
                            const char *p_name)
{
    int tmp;

    tmp = atoi(val_str);
    if (tmp < min || tmp > max) {
        fprintf(stderr, "%s(): invalid value %d for '%s' "
                "in %s, should be in range [%d-%d]\n",
                __func__, tmp, p_name, SRV_CONFIG_FILE, min, max);
        return -ERANGE;
    }

    *val = tmp;

    return 0;
}

static int parse_log_file_name(const char *val_str)
{
    SRV_CONFIG.log_file_name = (char *) malloc(strlen(val_str) + 1);
    if (!SRV_CONFIG.log_file_name) {
        fprintf(stderr, "%s(): could not allocate %ld bytes for "
                "log_file name %s in %s\n", __func__,
                strlen(val_str) + 1, val_str, SRV_CONFIG_FILE);
        return -ENOMEM;
    }

    strcpy(SRV_CONFIG.log_file_name, val_str);
    SRV_CONFIG.log_file_desc = fopen(val_str, "w");

    if (!SRV_CONFIG.log_file_desc) {
        fprintf(stderr, "%s(): could not open 'log_file'=%s "
                "from %s: %s\n", __func__, val_str, SRV_CONFIG_FILE,
                strerror(errno));
        return -errno;
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
        ret = parse_range_value(val_str, &val, 1, 65535, "port");
        if (ret)
            return ret;

        SRV_CONFIG.port = val;
    } else if (strcmp(key, "num_workers") == 0) {
        ret = parse_range_value(val_str, &val, 1, MAX_NUM_WORKERS,
                                "num_workers");
        if (ret)
            return ret;

        SRV_CONFIG.num_workers = val;
    } else if (strcmp(key, "log_file") == 0) {
        ret = parse_log_file_name(val_str);
        if (ret)
            return ret;
    } else if (strcmp(key, "log_level") == 0) {
        ret = parse_range_value(val_str, &val, LOG_ERROR, LOG_INFO2,
                                "log_level");
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
