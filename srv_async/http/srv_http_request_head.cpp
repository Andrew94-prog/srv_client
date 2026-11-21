/* HEAD requests: same routing as GET, but without body in response */

#include "srv_config.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"
#include "libfs/srv_fs.h"

response_ptr_t http_request_head_msg::ServeFileRequest()
{
    std::string full_path = SRV_CONFIG.base_dir + path;
    if (file_access_ok(full_path)) {
        response_ptr_t resp = make_http_response(*this, HTTP_STATUS_OK);
        resp->SetBodySize(file_size(full_path));

        return resp;
    } else {
        return make_http_response(*this, HTTP_STATUS_NOT_FOUND);
    }
}

response_ptr_t http_request_head_msg::ServeApiRequest()
{
    return make_http_response(*this, HTTP_STATUS_NOT_IMPLEMENTED);
}

response_ptr_t http_request_head_msg::ServeMethodRequest()
{
    if (!IsHostMatching()) {
        if (GetPath() == "/" || GetPath() == "/index.html") {
            return make_http_response(*this, HTTP_STATUS_OK);
        } else {
            return make_http_response(*this, HTTP_STATUS_NOT_FOUND);
        }
    }

    if (GetPath() == "/")
        path += "index.html";

    if (GetPath().starts_with("/api/"))
        return ServeApiRequest();
    else
        return ServeFileRequest();
}
