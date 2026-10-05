#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>    /* for getpid() in LOG macro */

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

/*
 * Receive single http request from client, form response for it
 * and send response back to client.
 * Raw data from client is received by recv_from_curr_conn() into
 * local "fast" buffer recv_buf, which is always enough for any
 * valid http headers (RECV_BUF_SIZE). Body is received into
 * recv_buf in cycle and appended to body field of request.
 * Returns received http_request_<method>_msg object or nullptr,
 * if connection was closed by client or by timeout
 */
std::shared_ptr<http_request_msg> recv_http_msg()
{
    char *recv_buf_p = curr_conn()->recv_buf_p;
    const size_t recv_buf_size = curr_conn()->recv_buf_size;
    size_t n_have = 0, hdr_end = 0;
    std::shared_ptr<http_request_msg> req;

    /* Receive http headers of request into recv_buf */
    while (true) {
        ssize_t count = recv_from_curr_conn(recv_buf_p + n_have,
                                            recv_buf_size - n_have,
                                            TIMEOUT_INF, true);
        if (count <= 0) {
            /* Connection was closed by client due to error */
            return nullptr;
        }
        n_have += count;

        hdr_end = find_http_headers_end(
                std::string_view(recv_buf_p, n_have));
        if (hdr_end)
            break;

        /* Restrict overall size of http headers by RECV_BUF_SIZE */
        if (n_have == recv_buf_size) {
            LOG(LOG_INFO1, "http headers of request are too large\n");
            return std::make_shared<http_request_msg>(
                    HTTP_STATUS_HEADER_FIELDS_TOO_LARGE);
        }
    }

    std::string_view recv_data(recv_buf_p, n_have);

    /* Get http method from headers and construct appropriate
     * http_request_<method>_msg object. For unsupported method
     * create() returns request with error */
    std::string_view method = get_method_token(recv_data);

    req = http_request_msg::Create(method);

    /* Parse all http headers into request class object.
     * Only header section is passed, remaining bytes in
     * recv_buf belong to request body. Malformed message
     * is marked by error_code inside ParseRequestHeader.
     * Skip parsing for requests already received with error */
    if (req->IsOk())
        req->ParseRequestHeader(recv_data.substr(0, hdr_end));

    /*
     * For those methods, which require body: get Content-Length
     * from headers and receive the remaining body of message
     * from client using recv_buf in cycle
     */
    if (req->IsOk() && http_request_msg::MethodHasBody(req->GetMethod())) {
        if (!req->HasHeader("Content-Length")) {
            LOG(LOG_INFO1, "no Content-Length header in request with"
                    " body, send response with error\n");
            req->SetErrorCode(HTTP_STATUS_LENGTH_REQUIRED);
        } else {
            /* Validity of Content-Length value was already checked
             * in ParseRequestHeader, so transformation always succeeds */
            size_t body_len = http_msg::AsSizeT(
                    req->GetHeaderValue("Content-Length"));

            if (body_len > MAX_HTTP_BODY_SIZE) {
                LOG(LOG_INFO1, "http body of request is too large:"
                        " %lu bytes, send response with error\n",
                        static_cast<unsigned long>(body_len));
                req->SetErrorCode(HTTP_STATUS_PAYLOAD_TOO_LARGE);
            } else {
                /* Part of body could be already received in recv_buf
                 * together with headers */
                size_t left = std::min(n_have - hdr_end, body_len);

                req->AppendBody(recv_data.substr(hdr_end, left));

                /* Receive the remaining body of message in cycle */
                while (req->GetBodySize() < body_len) {
                    size_t to_recv = std::min(body_len - req->GetBodySize(),
                                              recv_buf_size);
                    ssize_t count = recv_from_curr_conn(recv_buf_p, to_recv,
                                                        CLIENT_OP_TIMEOUT,
                                                        false);

                    if (count <= 0) {
                        /* Connection was closed in the middle of body */
                        return nullptr;
                    }
                    req->AppendBody(std::string_view(recv_buf_p, count));
                }
            }
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

    return send_to_curr_conn(msg.data(), static_cast<ssize_t>(msg.size()),
                             CLIENT_OP_TIMEOUT);
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
        req->GetMethod().c_str(), req->target.c_str(), req->version.c_str());
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

    return true;
}