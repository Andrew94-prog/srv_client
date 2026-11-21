/* POST requests: storing of received body in file with path
 * of request target. Also base class for PUT requests, which
 * use the same routing and differ in response status codes */

#include <string>

#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"
#include "libfs/srv_fs.h"

response_ptr_t http_request_post_msg::ServeFileRequest()
{
    /* Files can be modified only inside /storage/ directory */
    if (!path.starts_with("/storage/"))
        return make_http_response(*this, HTTP_STATUS_FORBIDDEN,
                "<p>Modifications are allowed only inside"
                " /storage/ path.</p>\n");

    std::string full_path = SRV_CONFIG.base_dir + path;

    /* Request target must not be a directory */
    if (dir_access_ok(full_path))
        return make_http_response(*this, HTTP_STATUS_CONFLICT,
                "<p>Requested path is a directory.</p>\n");

    /* Directory for storing of file must already exist,
     * it is not created by request */
    std::string parent_dir = full_path.substr(0,
            full_path.rfind('/'));
    if (!dir_access_ok(parent_dir))
        return make_http_response(*this, HTTP_STATUS_CONFLICT,
                "<p>Directory for file does not exist.</p>\n");

    bool file_existed = file_access_ok(full_path);

    if (upload_file(full_path, GetBody())) {
        LOG(LOG_ERROR, "could not store file %s\n",
                full_path.c_str());
        return make_http_response(*this,
                HTTP_STATUS_INTERNAL_SERVER_ERROR,
                "<p>Error while storing file.</p>\n");
    }

    return make_http_response(*this, HTTP_STATUS_OK,
            "<p>File " + path +
            (file_existed ? " replaced, " : " created, ") +
            std::to_string(GetBodySize()) + " bytes stored.</p>\n");
}

response_ptr_t http_request_post_msg::ServeApiRequest()
{
    return make_http_response(*this, HTTP_STATUS_NOT_IMPLEMENTED,
            "<p>API interface is not implemented yet.</p>\n");
}

response_ptr_t http_request_post_msg::ServeMethodRequest()
{
    /* Requests to another host are not served */
    if (!IsHostMatching())
        return make_http_response(*this, HTTP_STATUS_FORBIDDEN,
                "Forbidden");

    if (GetPath().starts_with("/api/"))
        return ServeApiRequest();

    return ServeFileRequest();
}
