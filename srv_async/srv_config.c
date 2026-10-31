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

/* Allocate copy of val_str and store it in *dst.
 * Value is replaced if parameter was already set */
static int parse_string_value(const char *val_str, char **dst,
                              const char *p_name)
{
    char *tmp;

    tmp = (char *) malloc(strlen(val_str) + 1);
    if (!tmp) {
        fprintf(stderr, "%s(): could not allocate %ld bytes for "
                "'%s' in %s\n", __func__, strlen(val_str) + 1,
                p_name, SRV_CONFIG_FILE);
        return -ENOMEM;
    }

    strcpy(tmp, val_str);
    free(*dst);
    *dst = tmp;

    return 0;
}

/* Parse 'base_dir' parameter: store copy of val_str in
 * SRV_CONFIG.base_dir. Relative path is prepended with server's
 * current working directory, absolute path is stored as is.
 * Value is replaced if parameter was already set */
static int parse_base_dir(const char *val_str)
{
    char *val, *cwd = NULL;
    size_t len;

    if (val_str[0] != '/') {
        cwd = getcwd(NULL, 0);
        if (cwd == NULL) {
            fprintf(stderr, "%s(): could not get current working "
                    "directory: %s\n", __func__, strerror(errno));
            return -errno;
        }

        /* current dir + '/' + relative base_dir + '\0' */
        len = strlen(cwd) + 1 + strlen(val_str) + 1;
        val = (char *) malloc(len);
        if (val == NULL) {
            free(cwd);
            fprintf(stderr, "%s(): could not allocate %ld bytes for "
                    "'base_dir' in %s\n", __func__, len,
                    SRV_CONFIG_FILE);
            return -ENOMEM;
        }
        snprintf(val, len, "%s/%s", cwd, val_str);
        free(cwd);
    } else {
        val = (char *) malloc(strlen(val_str) + 1);
        if (val == NULL) {
            fprintf(stderr, "%s(): could not allocate %ld bytes for "
                    "'base_dir' in %s\n", __func__,
                    strlen(val_str) + 1, SRV_CONFIG_FILE);
            return -ENOMEM;
        }
        strcpy(val, val_str);
    }

    free(SRV_CONFIG.base_dir);
    SRV_CONFIG.base_dir = val;

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
    } else if (strcmp(key, "base_dir") == 0) {
        ret = parse_base_dir(val_str);
        if (ret)
            return ret;
    } else if (strcmp(key, "host_name") == 0) {
        ret = parse_string_value(val_str, &SRV_CONFIG.host_name,
                                 "host_name");
        if (ret)
            return ret;
    } else if (strcmp(key, "db_socket") == 0) {
        ret = parse_string_value(val_str, &SRV_CONFIG.db_socket,
                                 "db_socket");
        if (ret)
            return ret;
    } else if (strcmp(key, "db_name") == 0) {
        ret = parse_string_value(val_str, &SRV_CONFIG.db_name,
                                 "db_name");
        if (ret)
            return ret;
    } else if (strcmp(key, "db_user") == 0) {
        ret = parse_string_value(val_str, &SRV_CONFIG.db_user,
                                 "db_user");
        if (ret)
            return ret;
    } else if (strcmp(key, "db_password") == 0) {
        ret = parse_string_value(val_str, &SRV_CONFIG.db_password,
                                 "db_password");
        if (ret)
            return ret;
    }

    return 0;
}

/* Check that base_dir path is accessible for read and write */
int validate_srv_base_dir(const char *path)
{
    if (access(path, R_OK | W_OK) != 0) {
        fprintf(stderr, "%s(): base_dir '%s' is not accessible for "
                "read and write: %s\n", __func__, path, strerror(errno));
        return -EACCES;
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
