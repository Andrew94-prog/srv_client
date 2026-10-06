/* Common class of http requests from clients */

#include <charconv>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

void http_request_msg::ParseRequestHeader(std::string_view buf)
{
    auto fail = [this]() {
        parse_error = true;
        SetErrorCode(HTTP_STATUS_BAD_REQUEST);
    };

    /* Parse request line: METHOD SP request-target SP HTTP-version CRLF */
    size_t line_end = buf.find(srv_http::CRLF);
    if (line_end == std::string_view::npos)
        return fail();

    std::string_view line = buf.substr(0, line_end);
    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 == std::string_view::npos || sp1 == sp2)
        return fail();

    std::string_view method_v = line.substr(0, sp1);
    std::string_view target_v = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string_view version_v = line.substr(sp2 + 1);
    if (method_v.empty() || target_v.empty() ||
            !version_v.starts_with("HTTP/"))
        return fail();
    method = method_v;
    target = target_v;
    version = version_v;

    /* Parse header fields: "field-name: OWS field-value OWS CRLF"
     * until end of buf (end of header section). For headers with
     * non-string value type check that value is valid and store
     * it as appropriate type of variant */
    std::string_view p = buf.substr(line_end + srv_http::CRLF.size());
    while (!p.empty()) {
        line_end = p.find(srv_http::CRLF);
        if (line_end == std::string_view::npos)
            return fail();

        line = p.substr(0, line_end);
        if (!line.empty()) {
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                return fail();

            std::string_view name = line.substr(0, colon);
            std::string_view value =
                    srv_http::trim_spaces(line.substr(colon + 1));

            if (srv_http::to_lower(name) == "content-length") {
                /* Content-Length value must be a decimal number */
                size_t len;
                auto [ptr, ec] = std::from_chars(value.data(),
                                                 value.data() + value.size(),
                                                 len);

                if (ec != std::errc() ||
                        ptr != value.data() + value.size())
                    return fail();
                AddHeader(name, len);
            } else {
                AddHeader(name, value);
            }
        }
        p.remove_prefix(std::min(line_end + srv_http::CRLF.size(), p.size()));
    }
}

bool http_request_msg::WantKeepAlive() const
{
    /* Close connection after response on any error */
    if (IsError())
        return false;

    if (HasHeader("Connection")) {
        const std::string conn_val =
                AsString(GetHeaderValue("Connection"));

        if (srv_http::contains_ci(conn_val, "close"))
            return false;
        if (srv_http::contains_ci(conn_val, "keep-alive"))
            return true;
    }

    return version != "HTTP/1.0";
}

/* Description of each http method supported by server */
const std::unordered_map<std::string, http_request_msg::method_info_t>
http_request_msg::methods_info = {
    {"GET",    {[] { return std::make_shared<http_request_get_msg>(); },
                false}},
    {"HEAD",   {[] { return std::make_shared<http_request_head_msg>(); },
                false}},
    {"POST",   {[] { return std::make_shared<http_request_post_msg>(); },
                true}},
    {"PUT",    {[] { return std::make_shared<http_request_put_msg>(); },
                true}},
    {"DELETE", {[] { return std::make_shared<http_request_delete_msg>(); },
                false}},
    {"PATCH",  {[] { return std::make_shared<http_request_patch_msg>(); },
                true}},
};

std::shared_ptr<http_request_msg> http_request_msg::Create(
        std::string_view method)
{
    auto it = methods_info.find(std::string(method));

    /* For method not supported by server return request with error */
    if (it == methods_info.end()) {
        LOG(LOG_INFO1, "unsupported http method in request:"
                " %.*s, send response with error\n",
            static_cast<int>(method.size()), method.data());
        return std::make_shared<http_request_msg>(
                HTTP_STATUS_METHOD_NOT_ALLOWED, method);
    }

    return it->second.creator();
}

bool http_request_msg::MethodHasBody(std::string_view method)
{
    auto it = methods_info.find(std::string(method));

    return it != methods_info.end() && it->second.has_body;
}

std::string http_request_msg::Serialize() const
{
    std::ostringstream out;

    out << method << " " << target << " " << version << srv_http::CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}

std::shared_ptr<http_response_msg> http_request_msg::MakeResponse()
{
    /* Response for invalid request or request received with error */
    if (IsError()) {
        return srv_http::make_error_response(
                GetErrorCode() != HTTP_STATUS_NONE ?
                GetErrorCode() : HTTP_STATUS_BAD_REQUEST, *this);
    }

    /* Dispatch to method-specific response for valid request */
    return MakeOkResponse();
}

std::shared_ptr<http_response_msg> http_request_msg::MakeOkResponse()
{
    /* Should not be called for valid requests of base class type */
    return srv_http::make_error_response(
            HTTP_STATUS_INTERNAL_SERVER_ERROR, *this);
}
