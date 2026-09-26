#ifndef SRV_DEFS_H
#define SRV_DEFS_H

/* Maximum size of http message body */
#define MAX_HTTP_BODY_SIZE	(32UL * 1024 * 1024)

#define STACK_SIZE		32768
#define RECV_BUF_SIZE	8192
#define GUARD_SIZE		4096
#define CTX_BUF_SIZE	(STACK_SIZE + RECV_BUF_SIZE + GUARD_SIZE)
#define MAX_ACTIVE_TIMEOUT	100000000
#define MAX_INACTIVE_TIMEOUT	3000000000
#define MAX_NUM_WORKERS		1000
#define DEFAULT_SRV_PORT	8080
#define DEFAULT_NUM_WORKERS	1
#define SRV_CONFIG_FILE		"srv.config"
#define MIN_CONN_CTX_CACHE_CNT	50
#define MAX_CONN_CTX_CACHE_CNT	300

#define TIMEOUT_INF ((time_t) -1)
#define CLIENT_OP_TIMEOUT 5000000000

enum {
	LOG_ERROR = 0,
	LOG_INFO1 = 1,
	LOG_INFO2 = 2
};

#define LOG(lvl, ...)							\
({												\
	FILE *fout = SRV_CONFIG.log_file_desc;		\
												\
	if (lvl <= SRV_CONFIG.log_level) {			\
		fprintf(fout, "(%d) ", getpid());		\
		switch (lvl) {							\
			case LOG_ERROR:						\
			fprintf(fout, "ERROR: ");			\
			break;								\
			case LOG_INFO1:						\
			fprintf(fout, "INFO1: ");			\
			break;								\
			case LOG_INFO2:						\
			fprintf(fout, "INFO2: ");			\
			break;								\
			default:							\
			fprintf(fout, "_____: ");			\
			break;								\
		}										\
		fprintf(fout, "%s(): ", __func__);		\
		fprintf(fout, __VA_ARGS__);				\
		if (lvl == LOG_ERROR)					\
			fflush(fout);						\
	}											\
})

#endif /* SRV_DEFS_H */