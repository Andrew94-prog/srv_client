#ifndef SRV_DEFS_H
#define SRV_DEFS_H

#include <limits.h>


//#define DBG

#ifdef DBG
#define pr_debug(...) 			\
({					\
	printf("(%d) ", getpid());	\
	printf(__VA_ARGS__);		\
})
#else
#define pr_debug
#endif

#define p_error(...)			\
({					\
	printf("(%d) ", getpid());	\
	perror(__VA_ARGS__);		\
})

#define RECV_BUF_SIZE		1024
#define SEND_BUF_SIZE		1024

#define STACK_SIZE		8192
#define CONN_TIMEOUT		5
#define MAX_ACCEPT_TRIES	100
#define MAX_NUM_WORKERS		1000

#endif /* SRV_DEFS_H */
