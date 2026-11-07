/* HEAD requests: same routing as GET, but without body in response */

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

http_response_ptr_t http_request_head_msg::ServeFileRequest()
{
    std::FILE *f = std::fopen(GetPath().c_str(), "r");
    if (f == nullptr)
        return make_http_error_response(HTTP_STATUS_NOT_FOUND, WantKeepAlive(),
                                    "Page or file not found");
    std::fseek(f, 0L, SEEK_END);
    std::size_t f_sz = ftell(f);
    std::fclose(f);

    return make_http_ok_response(WantKeepAlive(),
                                guess_mime_type(GetPath()),
                                f_sz);
}

http_response_ptr_t http_request_head_msg::ServeApiRequest()
{
    if (GetPath() != "/api/users")
        return make_http_error_response(HTTP_STATUS_NOT_FOUND, WantKeepAlive());

    /* Read pagination parameters with validation of their values */
    size_t page = 1, limit = 10;
    const std::string *param;

    if ((param = GetQueryParam("page")) != nullptr) {
        try {
            page = std::stoul(*param);
        } catch (...) {
            return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                                    WantKeepAlive());
        }
    }
    if ((param = GetQueryParam("limit")) != nullptr) {
        try {
            limit = std::stoul(*param);
        } catch (...) {
            return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                                WantKeepAlive());
        }
    }
    if (page == 0 || limit == 0 || limit > 100)
        return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                    WantKeepAlive());

    return make_http_ok_response(WantKeepAlive(),
                    "application/json; charset=UTF-8", 0);
}

http_response_ptr_t http_request_head_msg::ServeMethodRequest()
{
    if (!IsHostMatching()) {
        if (GetPath() == "/" || GetPath() == "/index.html") {
            return make_http_ok_response(WantKeepAlive(),
                                "text/html; charset=UTF-8", 0);
        } else {
            return make_http_error_response(HTTP_STATUS_NOT_FOUND,
                                            WantKeepAlive());
        }
    }

    if (GetPath().starts_with("/api/"))
        return ServeApiRequest();
    else
        return ServeFileRequest();
}
