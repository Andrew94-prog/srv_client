#ifndef SRV_CONFIG_H
#define SRV_CONFIG_H

#include <stdbool.h>

#define MAX_LINE_LEN 1024

typedef struct srv_config {
    int port;
    int num_workers;
    char *log_file_name;
    FILE *log_file_desc;
    int log_level;
    bool help;
} srv_config_t;

extern srv_config_t SRV_CONFIG;
extern const char HELP_MSG[];

int parse_srv_cmdline_opts(int argc, char *argv[]);
int parse_srv_config(void);

#endif /* SRV_CONFIG_H */
