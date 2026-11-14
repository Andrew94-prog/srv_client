/* GET requests: routing of request target to usage scenarios:
 * main page, static file, file download, api request */

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <iostream>

#include "srv_config.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"
#include "libfs/srv_fs.h"

response_ptr_t http_request_get_msg::ServeFileRequest()
{
    response_ptr_t resp = http_request_head_msg::ServeFileRequest();
    if (resp->IsOk()) {
        std::string full_path = SRV_CONFIG.base_dir + path;

        resp->SetBody(download_file(full_path));

        if (path.starts_with("/storage/"))
            resp->AddHeader("Content-Disposition",
                        "attachment; filename=\"" +
                        path + "\"");
    } else {
        resp->SetBody("Error while serving file request");
    }

    return resp;
}

response_ptr_t http_request_get_msg::ServeApiRequest()
{
    response_ptr_t resp = http_request_head_msg::ServeApiRequest();

    resp->SetBody("API interface is not impelemented");

    return resp;
}

response_ptr_t http_request_get_msg::ServeMethodRequest()
{
    if (!IsHostMatching()) {
        if (GetPath() == "/" || GetPath() == "/index.html") {
            return make_http_response(*this, HTTP_STATUS_OK,
                "<h1>Welcome to coroutine-based async server!</h1>\n"
                "<p>This is the server's main page plug.</p>\n");
        } else {
            return make_http_response(*this, HTTP_STATUS_NOT_FOUND,
                        "<p>Requested page: " + GetPath() + "</p>\n");
        }
    }

    if (GetPath() == "/")
        path += "index.html";

    if (GetPath().starts_with("/api/")) {
        return ServeApiRequest();
    } else {
        return ServeFileRequest();
    }
}
