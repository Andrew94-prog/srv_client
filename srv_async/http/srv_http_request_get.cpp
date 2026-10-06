/* GET requests: server's main page */

#include <memory>
#include <string>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

namespace srv_http {

response_ptr_t make_main_page_response(const http_request_msg &req,
                                       bool omit_body)
{
    if (req.target == "/" || req.target == "/index.html") {
        return make_page_response(HTTP_STATUS_OK, req, "Main Page",
                "<h1>Welcome to coroutine-based async server!</h1>\n"
                "<p>This is the server's main page plug.</p>\n",
                omit_body);
    }

    return make_page_response(HTTP_STATUS_NOT_FOUND, req, "Not Found",
            "<h1>404 Page Not Found</h1>\n"
            "<p>Requested page: " + req.target + "</p>\n",
            omit_body);
}

} // namespace srv_http

std::shared_ptr<http_response_msg> http_request_get_msg::MakeOkResponse()
{
    return srv_http::make_main_page_response(*this, false);
}
