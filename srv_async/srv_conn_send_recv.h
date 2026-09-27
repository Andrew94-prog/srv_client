#ifndef SRV_CONN_SEND_RECV_H
#define SRV_CONN_SEND_RECV_H

#include <time.h>
#include <stddef.h>

ssize_t recv_from_curr_conn(char *buf, ssize_t to_recv,
                            time_t timeout, bool terminator);
ssize_t send_to_curr_conn(const char *buf, ssize_t to_send,
                        time_t timeout);

#endif /* SRV_CONN_SEND_RECV_H */