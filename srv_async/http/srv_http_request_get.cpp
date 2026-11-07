/* GET requests: routing of request target to usage scenarios:
 * main page, static file, file download, api request */

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "srv_config.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"



http_response_ptr_t http_request_get_msg::ServeFileRequest()
{
    std::FILE *f = std::fopen(GetPath().c_str(), "rb");
    if (f == nullptr)
        return make_http_error_response(HTTP_STATUS_NOT_FOUND, WantKeepAlive(),
                                    "Page or file not found");

    std::string content;
    char buf[4096];
    size_t n;

    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        content.append(buf, n);
    bool read_err = std::ferror(f) != 0;
    std::fclose(f);

    if (read_err)
        return make_http_error_response(HTTP_STATUS_NOT_FOUND, WantKeepAlive(),
                                    "Failed to read page or file");

    auto resp = make_http_ok_response(WantKeepAlive(),
                                    guess_mime_type(GetPath()),
                                    content);

    /*resp->AddHeader("Content-Disposition",
                "attachment; filename=\"" +
                GetPath() + "\"");*/

    return resp;
}

http_response_ptr_t http_request_get_msg::ServeApiRequest()
{
    if (GetPath() != "/api/users")
        return make_http_error_response(HTTP_STATUS_NOT_FOUND, WantKeepAlive(),
                                    "{\"error\": \"unknown api endpoint\"}");

    /* Read pagination parameters with validation of their values */
    size_t page = 1, limit = 10;
    const std::string *param;

    if ((param = GetQueryParam("page")) != nullptr) {
        try {
            page = std::stoul(*param);
        } catch (...) {
            return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                                    WantKeepAlive(),
                                    "{\"error\": \"invalid page parameter\"}");
        }
    }
    if ((param = GetQueryParam("limit")) != nullptr) {
        try {
            limit = std::stoul(*param);
        } catch (...) {
            return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                                WantKeepAlive(),
                                "{\"error\": \"invalid limit parameter\"}");
        }
    }
    if (page == 0 || limit == 0 || limit > 100)
        return make_http_error_response(HTTP_STATUS_BAD_REQUEST,
                    WantKeepAlive(),
                    "{\"error\": \"page must be >= 1, limit in [1..100]\"}");

    std::ostringstream json;

    json << "{\"page\": " << page << ", \"limit\": " << limit
         << ", \"items\": [";
    for (size_t i = 0; i < 2; i++) {
        size_t id = (page - 1) * limit + i + 1;

        if (i != 0)
            json << ", ";
        json << "{\"id\": " << id << ", \"name\": \"user" << id
             << "\"}";
    }
    json << "]}";
    return make_http_ok_response(WantKeepAlive(),
                    "application/json; charset=UTF-8",
                    json.str());
}

http_response_ptr_t http_request_get_msg::ServeMethodRequest()
{
    if (!IsHostMatching()) {
        if (GetPath() == "/" || GetPath() == "/index.html") {
            return make_http_ok_response(WantKeepAlive(),
                "text/html; charset=UTF-8",
                "<h1>Welcome to coroutine-based async server!</h1>\n"
                "<p>This is the server's main page plug.</p>\n");
        } else {
            return make_http_error_response(HTTP_STATUS_NOT_FOUND,
                        WantKeepAlive(),
                        "<p>Requested page: " + GetPath() + "</p>\n");
        }
    }

    if (GetPath().starts_with("/api/"))
        return ServeApiRequest();
    else
        return ServeFileRequest();
}
