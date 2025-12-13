#ifndef SRV_SOCK_H
#define SRV_SOCK_H

int set_nonblock(int sock);
int set_async(int sock);
int create_listening_socket(int srv_port);

#endif /* SRV_SOCK_H */
