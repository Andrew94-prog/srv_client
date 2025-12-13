#ifndef SRV_DEFS_H
#define SRV_DEFS_H

//#define DBG

#ifdef DBG
#define pr_debug(...) 			\
({					\
	printf("(%d) ", getpid());	\
	printf(__VA_ARGS__);		\
})
#else
#define pr_debug(...)
#endif

#define p_error(...)			\
({					\
	printf("(%d) ", getpid());	\
	perror(__VA_ARGS__);		\
})

#define RECV_BUF_SIZE		1024
#define SEND_BUF_SIZE		1024

#define STACK_SIZE		8192
#define MAX_ACTIVE_TIMEOUT	1
#define MAX_INACTIVE_TIMEOUT	3
#define MAX_NUM_WORKERS		1000

#define HTTP_RESPONSE_MSG "HTTP/1.1 200 OK\n" \
			"Content-Type: text/html; charset=UTF-8\n" \
			"Content-Length: 156\n" \
			"Date: Sat, 04 Oct 2025 14:51:00 GMT\n" \
			"Server: my_srv_asyn/1.0 (Ubuntu)\n" \
			"\n" \
			"<!DOCTYPE html>\n" \
			"<html>\n" \
			"<head>\n" \
			"    <title>Example Page</title>\n" \
			"</head>\n" \
			"<body>\n" \
			"    <h1>Welcome!</h1>\n" \
			"    <p>This is an example HTML page.</p>\n" \
			"</body>\n" \
			"</html>\n"

#endif /* SRV_DEFS_H */
