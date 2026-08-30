#ifndef SRV_CONFIG_H
#define SRV_CONFIG_H

#include <stdbool.h>

typedef struct srv_config {
    int port;
    int num_workers;
    bool help;
} srv_config_t;

extern srv_config_t SRV_CONFIG;
extern const char HELP_MSG[];

int parse_srv_cmdline_opts(int argc, char *argv[]);
int parse_srv_config(void);

#endif /* SRV_CONFIG_H */
