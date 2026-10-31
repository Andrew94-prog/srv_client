#ifndef SRV_CONFIG_H
#define SRV_CONFIG_H

#define MAX_LINE_LEN 1024

/* Help message with expected format of config file lines,
 * printed if config file could not be parsed */
#define HELP_MSG \
    "Expected format of config file lines:\n" \
    "port: PORT_NUM (1-65535)\n" \
    "num_workers: NUM_WORKERS (1-%d)\n" \
    "log_file: path_to_log_file\n" \
    "log_level: LOG_LEVEL (0-%d)\n"

typedef struct srv_config {
    int port;
    int num_workers;
    char *log_file_name;
    FILE *log_file_desc;
    int log_level;
} srv_config_t;

extern srv_config_t SRV_CONFIG;

int parse_srv_config(void);

#endif /* SRV_CONFIG_H */
