#ifndef SRV_HTTP_SEND_RECV_H
#define SRV_HTTP_SEND_RECV_H

#include <memory>

#include "srv_http_msg.h"

std::shared_ptr<http_request_msg> recv_http_msg(void);
ssize_t send_http_msg(std::shared_ptr<http_response_msg> resp);

bool handle_one_client_request(void);

#endif /* SRV_HTTP_SEND_RECV_H */
