#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "srv_send_recv.h"
#include "srv_conn_ctxt.h"
#include "srv_config.h"
#include "srv_defs.h"
#include "http_msg.h"

static bool is_http_message_end(char *recv_buf, ssize_t n_recv)
{
    return n_recv >= 4 && recv_buf[n_recv - 1] == '\n' &&
           recv_buf[n_recv - 2] == '\r' &&
           recv_buf[n_recv - 3] == '\n' &&
           recv_buf[n_recv - 4] == '\r';
}

/*
 * Find end of http header section (empty line "\r\n\r\n") in buf.
 * Returns offset just past "\r\n\r\n" or 0 if it is not found
 */
static size_t find_http_headers_end(const char *buf, size_t len, size_t prev_len)
{
    if (len < 4)
        return 0;

    for (size_t i = prev_len; i <= len - 4; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }

    return 0;
}

/* Extract first token (http method) from request line in buf */
static std::string get_method_token(const char *buf, size_t len)
{
    const char *sp = (const char *)memchr(buf, ' ', len);

    if (!sp)
        return std::string();
    return std::string(buf, sp - buf);
}

static ssize_t recv_from_curr_conn(char *buf, ssize_t to_recv)
{
    ssize_t n_recv = 0, count;
    bool recv_ended = false;
    int ret;

    curr_conn_update_active();
    while (to_recv && !recv_ended) {
        count = read(curr_conn_sock(), buf, to_recv);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_timeout(MAX_ACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "recv async, set inactivate "
                            "conn_sock = %d, curr_conn = %p\n",
                            curr_conn_sock(), curr_conn());
                    curr_conn_set_inactive();
                }

                LOG(LOG_INFO2, "no data to recv from sock, switch to main ctxt"
                    " conn_sock = %d, curr_conn = %p\n",
                    curr_conn_sock(), curr_conn());

                ret = swap_to_main_ctx();
                if (ret == 0) {
                    continue;
                } else {
                    LOG(LOG_ERROR, "switch ctx from client recv to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                LOG(LOG_ERROR, "recv from client failed, close connection"
                       " and switch back to main_ctx");
                curr_conn_close();
                swap_to_main_ctx();
                /* Should never get here*/
                return -1;
            }
        } else if (count > 0) {
            n_recv += count;
            to_recv -= count;

            curr_conn_update_active();
            recv_ended = is_http_message_end(buf, n_recv);
        } else {
            LOG(LOG_ERROR, "recv 0 bytes from client, end\n");
            recv_ended = true;
        }
    }

    return n_recv;
}

static ssize_t send_to_curr_conn(const char *buf, ssize_t to_send)
{
    ssize_t n_send = 0, count;
    bool send_ended = false;
    int ret;

    while (to_send && !send_ended) {
        count = write(curr_conn_sock(), buf, to_send);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (curr_conn_timeout(MAX_ACTIVE_TIMEOUT)) {
                    LOG(LOG_INFO2, "send async, set inactive "
                            "conn_sock = %d, curr_conn = %p\n",
                            curr_conn_sock(), curr_conn());
                    curr_conn_set_inactive();
                }

                LOG(LOG_INFO2, "no data to send to sock, switch to main ctxt"
                    " conn_sock = %d, curr_conn = %p\n",
                    curr_conn_sock(), curr_conn());

                ret = swap_to_main_ctx();
                if (ret == 0) {
                    continue;
                } else {
                    LOG(LOG_ERROR, "switch ctx from client send to"
                           " main_ctx failed with unknown error");
                    exit(EXIT_FAILURE);
                }
            } else {
                LOG(LOG_ERROR, "write to client failed, close connection"
                       " and switch back to main_ctx");
                curr_conn_close();
                swap_to_main_ctx();
                /* Should never get here*/
                return -1;
            }
        } else if (count > 0) {
            n_send += count;
            to_send -= count;

            curr_conn_update_active();
        } else {
            LOG(LOG_ERROR, "sent 0 bytes to client, end\n");
            send_ended = true;
        }
    }

    return n_send;
}

/*
 * Receive single http request from client, form response for it
 * and send response back to client.
 * Raw data from client is received by recv_from_curr_conn() into
 * local "fast" buffer recv_buf, which is always enough for any
 * valid http headers (RECV_BUF_SIZE). Body is received into
 * recv_buf in cycle and appended to body field of request.
 * Returns received http_request_<method>_msg object or NULL,
 * if connection was closed by client or by timeout
 */
std::shared_ptr<http_request_msg> recv_http_msg(void)
{
    char recv_buf[RECV_BUF_SIZE];
    ssize_t n_have = 0, hdr_end = 0, prev_n_have, count;
    std::shared_ptr<http_request_msg> req;

    /* Receive http headers of request into recv_buf */
    while (1) {
        count = recv_from_curr_conn(recv_buf + n_have,
                                    RECV_BUF_SIZE - n_have);
        if (count <= 0) {
            /* Connection was closed by client due to error */
            return NULL;
        }
        prev_n_have = n_have;
        n_have += count;

        hdr_end = find_http_headers_end(recv_buf, n_have, prev_n_have);
        if (hdr_end)
            break;

        /* Restrict overall size of http headers by RECV_BUF_SIZE */
        if (n_have == RECV_BUF_SIZE) {
            LOG(LOG_INFO1, "http headers of request are too large\n");
            req = std::make_shared<http_request_msg>();
            req->error_code = 431;
            return req;
        }
    }

    /* Get http method from headers and construct appropriate
     * http_request_<method>_msg object */
    std::string method = get_method_token(recv_buf, n_have);

    req = http_request_msg::create(method);
    if (req) {
        /* Parse all http headers into request class object.
         * Only header section is passed, remaining bytes in
         * recv_buf belong to request body */
        if (req->parse_request(recv_buf, hdr_end) ==
            http_msg::PARSE_ERROR)
            req->error_code = 400;
    } else {
        /* Method is not supported by server */
        LOG(LOG_INFO1, "unsupported http method in request,"
            " send response with error\n");
        req = std::make_shared<http_request_msg>();
        req->method = method;
        req->error_code = 405;
    }

    /*
     * For those methods, which require body: get Content-Length
     * from headers and receive the remaining body of message
     * from client using recv_buf in cycle
     */
    if (!req->error_code && http_request_msg::method_has_body(req->method)) {
        const std::string *cl_hdr = req->find_header("Content-Length");
        unsigned long body_len;
        char *endp;

        if (!cl_hdr) {
            LOG(LOG_INFO1, "no Content-Length header in request with"
                    " body, send response with error\n");
            req->error_code = 411;
        } else {
            body_len = strtoul(cl_hdr->c_str(), &endp, 10);
            if (endp == cl_hdr->c_str() || *endp != '\0') {
                req->error_code = 400;
            } else if (body_len > MAX_HTTP_BODY_SIZE) {
                LOG(LOG_INFO1, "http body of request is too large:"
                        " %lu bytes, send response with error\n",
                        body_len);
                req->error_code = 413;
            } else {
                size_t left = n_have - hdr_end;

                /* Part of body could be already received in recv_buf
                 * together with headers */
                if (left > body_len)
                    left = body_len;
                req->append_body(recv_buf + hdr_end, left);

                /* Receive the remaining body of message in cycle */
                while (req->get_body_size() < body_len) {
                    size_t to_recv = body_len - req->get_body_size();
                    ssize_t count;

                    if (to_recv > RECV_BUF_SIZE)
                        to_recv = RECV_BUF_SIZE;
                    count = recv_from_curr_conn(recv_buf, to_recv);
                    if (count <= 0) {
                        /* Connection was closed in the middle of body */
                        return std::shared_ptr<http_request_msg>();
                    }
                    req->append_body(recv_buf, count);
                    curr_conn_update_active();
                }
            }
        }
    }

    return req;
}

/*
 * Send http response to client. Response is serialized to string
 * and sent by send_to_curr_conn() using raw c_str() pointer
 */
ssize_t send_http_msg(std::shared_ptr<http_response_msg> resp)
{
    std::string msg = resp->serialize();
    const char *data = msg.c_str();
    ssize_t to_send = (ssize_t) msg.size();

    return send_to_curr_conn(data, to_send);
}

bool handle_one_client_request(void)
{
    std::shared_ptr<http_request_msg> req = recv_http_msg();

    if (!req) {
        LOG(LOG_INFO1, "failed to recv request from client"
            " conn_sock = %d, conn = %p\n",
            curr_conn_sock(), curr_conn());
        return false;
    }

    LOG(LOG_INFO1, "got request from client conn_sock = %d, conn = %p\n",
        curr_conn_sock(), curr_conn());
    LOG(LOG_INFO1, "requset: method = %s, target = %s, version = %s\n",
        req->method.c_str(), req->target.c_str(), req->version.c_str());
    LOG(LOG_INFO2, "%s\n", req->serialize().c_str());

    std::shared_ptr<http_response_msg> resp = req->make_response();
    if (send_http_msg(resp) <= 0) {
        LOG(LOG_ERROR, "failed to send response to client"
            " conn_sock = %d, conn = %p\n",
            curr_conn_sock(), curr_conn());
        return false;
    }

    LOG(LOG_INFO1, "sent request to client conn_sock = %d, conn = %p\n",
        curr_conn_sock(), curr_conn());
    LOG(LOG_INFO1, "response: code = %d, %s\n", resp->status_code,
        resp->status_text.c_str());
    LOG(LOG_INFO2, "%s\n", resp->serialize().c_str());

    if (!req->want_keep_alive()) {
        LOG(LOG_INFO1, "client requested connection close\n");
        return false;
    }

    return true;
}