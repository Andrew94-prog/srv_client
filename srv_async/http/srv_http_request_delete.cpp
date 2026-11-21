/* DELETE requests: removal of file with path of request
 * target from base directory of server */

#include <string>

#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"
#include "libfs/srv_fs.h"

response_ptr_t http_request_delete_msg::ServeFileRequest()
{
    /* Files can be modified only inside /storage/ directory */
    if (!path.starts_with("/storage/"))
        return make_http_response(*this, HTTP_STATUS_FORBIDDEN,
                "<p>Modifications are allowed only inside"
                " /storage/ path.</p>\n");

    std::string full_path = SRV_CONFIG.base_dir + path;

    /* Only regular files are removed, directories
     * are not affected by DELETE requests */
    if (!file_access_ok(full_path))
        return make_http_response(*this, HTTP_STATUS_NOT_FOUND,
                "<p>Requested file does not exist.</p>\n");

    if (remove_file(full_path)) {
        LOG(LOG_ERROR, "could not remove file %s\n",
                full_path.c_str());
        return make_http_response(*this,
                HTTP_STATUS_INTERNAL_SERVER_ERROR,
                "<p>Error while removing file.</p>\n");
    }

    return make_http_response(*this, HTTP_STATUS_NO_CONTENT);
}

response_ptr_t http_request_delete_msg::ServeApiRequest()
{
    return make_http_response(*this, HTTP_STATUS_NOT_IMPLEMENTED,
            "<p>API interface is not implemented yet.</p>\n");
}

response_ptr_t http_request_delete_msg::ServeMethodRequest()
{
    /* Requests to another host are not served */
    if (!IsHostMatching())
        return make_http_response(*this, HTTP_STATUS_FORBIDDEN,
                "Forbidden");

    if (GetPath().starts_with("/api/"))
        return ServeApiRequest();

    return ServeFileRequest();
}
