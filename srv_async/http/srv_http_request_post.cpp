/* POST requests: plug response, will be implemented
 * in the next version of server */

#include <memory>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

std::shared_ptr<http_response_msg> http_request_post_msg::MakeOkResponse()
{
    return srv_http::make_page_response(HTTP_STATUS_NOT_IMPLEMENTED,
            *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of POST requests will be implemented"
            " in the next version of server.</p>\n", false);
}
