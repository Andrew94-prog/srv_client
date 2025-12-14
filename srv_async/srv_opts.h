#ifndef SRV_OPTS_H
#define SRV_OPTS_H

#include <stdbool.h>

typedef struct srv_opts {
    int port;
    int num_workers;
    bool help;
} srv_opts_t;

extern srv_opts_t SRV_OPTS;
extern const char HELP_MSG[];

int parse_srv_opts(int argc, char *argv[]);

#endif /* SRV_OPTS_H */
