/* PATCH requests: plug response, will be implemented
 * in the next version of server */

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

response_ptr_t http_request_patch_msg::ServeMethodRequest()
{
    /* Requests to another host are not served */
    if (!IsHostMatching())
        return make_http_response(*this, HTTP_STATUS_FORBIDDEN, "Forbidden");

    return make_http_response(*this, HTTP_STATUS_NOT_IMPLEMENTED,
                            "<p>Handling of PATCH requests will be implemented"
                            " in the next version of server.</p>\n");
}
