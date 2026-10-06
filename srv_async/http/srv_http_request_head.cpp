/* HEAD requests: same as GET, but without body in response */

#include <memory>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

std::shared_ptr<http_response_msg> http_request_head_msg::MakeOkResponse()
{
    return srv_http::make_main_page_response(*this, true);
}
