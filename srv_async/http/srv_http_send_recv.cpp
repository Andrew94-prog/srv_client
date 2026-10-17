#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>    /* for getpid() in LOG macro */
#include <cstring>

#include "srv_conn_send_recv.h"
#include "srv_http_send_recv.h"
#include "srv_conn_ctxt.h"
#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"

namespace {

constexpr std::string_view HEADERS_END = "\r\n\r\n";

/*
 * Find end of http header section (empty line "\r\n\r\n") in buf.
 * Returns offset just past "\r\n\r\n" or 0 if it is not found
 */
size_t find_http_headers_end(std::string_view buf)
{
    size_t pos = buf.find(HEADERS_END);

    return pos == std::string_view::npos ? 0 : pos + HEADERS_END.size();
}

/* Extract first token (http method) from request line in buf */
std::string_view get_method_token(std::string_view buf)
{
    size_t sp = buf.find(' ');

    return sp == std::string_view::npos ? std::string_view()
                                        : buf.substr(0, sp);
}

} // namespace


static std::string_view recv_http_hdr()
{
    char *recv_buf_p = curr_conn()->recv_buf_p;
    ssize_t recv_buf_size = curr_conn()->recv_buf_size;
    ssize_t n_have = curr_conn()->recv_buf_have;
    ssize_t n_curr = curr_conn()->recv_buf_curr;
    ssize_t hdr_size = 0, count;

    if (n_curr != n_have) {
        hdr_size = find_http_headers_end(
                    std::string_view(recv_buf_p + n_curr, n_have - n_curr));
        if (!hdr_size) {
            std::memmove(recv_buf_p, recv_buf_p + n_curr, n_have - n_curr);
            n_have = n_have - n_curr;
            n_curr = 0;
            count = recv_from_curr_conn(recv_buf_p, recv_buf_size - n_have,
                                        n_have, TIMEOUT_INF, true);
            if (count <= 0)
                return std::string_view();
            n_have += count;
        }
    } else {
        n_curr = n_have = 0;
        count = recv_from_curr_conn(recv_buf_p, recv_buf_size, 0,
                                    TIMEOUT_INF, true);
        if (count <= 0)
            return std::string_view();
        n_have += count;
    }

    if (!hdr_size)
        hdr_size = find_http_headers_end(std::string_view(
                            recv_buf_p + n_curr, n_have - n_curr));

    curr_conn()->recv_buf_have = n_have;
    curr_conn()->recv_buf_curr = n_curr + hdr_size;

    if (hdr_size)
        return std::string_view(recv_buf_p + n_curr, hdr_size);
    else
        return std::string_view();
}

std::shared_ptr<http_request_msg> recv_http_msg()
{
    std::string_view http_hdr = recv_http_hdr();
    if (!http_hdr.size())
        return nullptr;

    if (http_hdr.size() > MAX_HTTP_HDR_SZIE) {
        LOG(LOG_INFO1, "http headers of request are too large\n");
        return std::make_shared<http_request_msg>(
                    HTTP_STATUS_HEADER_FIELDS_TOO_LARGE);
    }

    std::string_view method = get_method_token(http_hdr);
    std::shared_ptr<http_request_msg> req = http_request_msg::Create(method);
    if (req->IsOk())
        req->ParseRequestHeader(http_hdr);

    if (req->IsOk() && http_request_msg::MethodHasBody(method)) {
        char *recv_buf_p = curr_conn()->recv_buf_p;
        ssize_t recv_buf_size = curr_conn()->recv_buf_size;
        ssize_t n_have = curr_conn()->recv_buf_have;
        ssize_t n_curr = curr_conn()->recv_buf_curr;
        ssize_t body_len = http_msg::AsSizeT(
                    req->GetHeaderValue("Content-Length"));
        ssize_t got_len = std::min(n_have - n_curr, body_len);

        req->AppendBody(std::string_view(recv_buf_p + n_curr, got_len));
        n_curr = n_curr + got_len;

        if (n_curr == n_have)
            n_have = n_curr = 0;

        curr_conn()->recv_buf_have = n_have;
        curr_conn()->recv_buf_curr = n_curr;

        while (got_len < body_len) {
            ssize_t to_recv = std::min(body_len - got_len, recv_buf_size);
            ssize_t count = recv_from_curr_conn(recv_buf_p, to_recv,
                                                0, CLIENT_OP_TIMEOUT,
                                                false);
            if (count != to_recv)
                return nullptr;

            req->AppendBody(std::string_view(recv_buf_p, count));
            got_len += count;

            cond_swap_to_main_ctx();
        }
    }

    return req;
}

/*
 * Send http response to client. Response is serialized to string
 * and sent by send_to_curr_conn() using raw data() pointer
 */
ssize_t send_http_msg(const std::shared_ptr<http_response_msg> &resp)
{
    const std::string msg = resp->Serialize();
    ssize_t curr = 0, size = msg.size();

    while (curr < size) {
        ssize_t to_send = std::min(SEND_PACKET_SIZE, size - curr);
        ssize_t count = send_to_curr_conn(msg.data() + curr, to_send,
                                        CLIENT_OP_TIMEOUT);
        if (count != to_send)
            return 0;

        curr += to_send;

        cond_swap_to_main_ctx();
    }

    return size;
}

bool handle_one_client_request()
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
        req->GetMethod().c_str(), req->GetTarget().c_str(),
        req->GetVersion().c_str());
    LOG(LOG_INFO2, "%s\n", req->Serialize().c_str());

    std::shared_ptr<http_response_msg> resp = req->MakeResponse();
    if (send_http_msg(resp) <= 0) {
        LOG(LOG_ERROR, "failed to send response to client"
            " conn_sock = %d, conn = %p\n",
            curr_conn_sock(), curr_conn());
        return false;
    }

    LOG(LOG_INFO1, "sent request to client conn_sock = %d, conn = %p\n",
        curr_conn_sock(), curr_conn());
    LOG(LOG_INFO1, "response: code = %d, %s\n", static_cast<int>(resp->status_code),
        resp->status_text.c_str());
    LOG(LOG_INFO2, "%s\n", resp->Serialize().c_str());

    if (!req->WantKeepAlive()) {
        LOG(LOG_INFO1, "client requested connection close\n");
        return false;
    }

    cond_swap_to_main_ctx();

    return true;
}