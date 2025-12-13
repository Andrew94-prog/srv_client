#ifndef SRV_SEND_RECV_H
#define SRV_SEND_RECV_H

ssize_t recv_http_msg(char *recv_buf, ssize_t to_recv);
ssize_t send_http_msg(const char *send_buf, ssize_t to_send);

#endif /* SRV_SEND_RECV_H */
