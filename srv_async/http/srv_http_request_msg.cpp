/* Common class of http requests from clients */

#include <charconv>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

void http_request_msg::ParseRequestHeader(std::string_view buf)
{
    auto fail = [this](http_status status) {
        SetErrorCode(status);
    };

    /* Parse request line: METHOD SP request-target SP HTTP-version CRLF */
    size_t line_end = buf.find(HTTP_CRLF);
    if (line_end == std::string_view::npos)
        return fail(HTTP_STATUS_BAD_REQUEST);

    std::string_view line = buf.substr(0, line_end);
    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 == std::string_view::npos || sp1 == sp2)
        return fail(HTTP_STATUS_BAD_REQUEST);

    std::string_view method_v = line.substr(0, sp1);
    std::string_view target_v = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string_view version_v = line.substr(sp2 + 1);
    if (method_v.empty() || target_v.empty() ||
            !version_v.starts_with("HTTP/"))
        return fail(HTTP_STATUS_BAD_REQUEST);
    method = method_v;
    target = target_v;
    version = version_v;

    /* Parse target of request, valid for all methods */
    ParseTarget();

    /* Parse header fields: "field-name: OWS field-value OWS CRLF"
     * until end of buf (end of header section). For headers with
     * non-string value type check that value is valid and store
     * it as appropriate type of variant */
    std::string_view p = buf.substr(line_end + HTTP_CRLF.size());
    while (!p.empty()) {
        line_end = p.find(HTTP_CRLF);
        if (line_end == std::string_view::npos)
            return fail(HTTP_STATUS_BAD_REQUEST);

        line = p.substr(0, line_end);
        if (!line.empty()) {
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                return fail(HTTP_STATUS_BAD_REQUEST);

            std::string_view name = line.substr(0, colon);
            std::string_view value =
                    trim_spaces(line.substr(colon + 1));

            if (to_lower(name) == "host") {
                host = value;
                AddHeader(name, value);
            } else if (to_lower(name) == "content-length") {
                if (!http_request_msg::MethodHasBody(method))
                    return fail(HTTP_STATUS_BAD_REQUEST);

                /* Content-Length value must be a decimal number */
                size_t len;
                auto [ptr, ec] = std::from_chars(value.data(),
                                                 value.data() + value.size(),
                                                 len);

                if (ec != std::errc() ||
                        ptr != value.data() + value.size())
                    return fail(HTTP_STATUS_BAD_REQUEST);
                if (len > MAX_HTTP_BODY_SIZE)
                    return fail(HTTP_STATUS_PAYLOAD_TOO_LARGE);
                AddHeader(name, len);
            } else {
                AddHeader(name, value);
            }
        }
        p.remove_prefix(std::min(line_end + HTTP_CRLF.size(), p.size()));
    }

    if (http_request_msg::MethodHasBody(method) &&
            !HasHeader("content-length"))
        return fail(HTTP_STATUS_LENGTH_REQUIRED);
}

void http_request_msg::ParseTarget()
{
    /* Server works directly with clients and supports only
     * origin-form of request target */
    if (!target.starts_with('/'))
        return SetErrorCode(HTTP_STATUS_BAD_REQUEST);

    /* Split target into path and query string at first '?' */
    size_t qmark = target.find('?');

    if (qmark != std::string::npos) {
        path = target.substr(0, qmark);
        query = target.substr(qmark + 1);
    } else {
        path = target;
    }

    std::string decoded;

    if (!PercentDecode(path, false, decoded))
        return SetErrorCode(HTTP_STATUS_BAD_REQUEST);

    /* Decoded path must not contain control characters or NUL */
    for (unsigned char c : decoded)
        if (c < 0x20 || c == 0x7f)
            return SetErrorCode(HTTP_STATUS_BAD_REQUEST);

    /* Normalize path: process segment by segment, remove ".",
     * ".." (with removal of previous segment) and empty segments.
     * ".." at root level means traversal above root */
    std::string normalized;
    std::vector<std::string_view> segments;
    size_t pos = 0;

    while (pos < decoded.size()) {
        size_t next = decoded.find('/', pos);
        std::string_view seg(decoded.data() + pos,
                (next == std::string::npos ? decoded.size() : next) - pos);

        if (seg == "..") {
            if (segments.empty())
                return SetErrorCode(HTTP_STATUS_BAD_REQUEST);
            segments.pop_back();
        } else if (seg != "." && !seg.empty()) {
            segments.push_back(seg);
        }
        pos = (next == std::string::npos) ? decoded.size() : next + 1;
    }

    for (const auto &seg : segments) {
        normalized += '/';
        normalized.append(seg);
    }
    if (normalized.empty())
        normalized = "/";
    path = std::move(normalized);
}

bool http_request_msg::PercentDecode(std::string_view in,
                                     bool plus_as_space,
                                     std::string &out)
{
    out.clear();

    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];

        if (c == '+') {
            out += plus_as_space ? ' ' : '+';
        } else if (c == '%') {
            if (i + 2 >= in.size())
                return false;

            auto hex_digit = [](char h) -> int {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return -1;
            };

            int hi = hex_digit(in[i + 1]);
            int lo = hex_digit(in[i + 2]);

            if (hi < 0 || lo < 0)
                return false;
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else {
            out += c;
        }
    }
    return true;
}

void http_request_msg::ParseQueryParams() const
{
    if (query_parsed)
        return;
    query_parsed = true;

    /* Query string is a sequence of key=value pairs separated
     * by '&'. Key and value are percent-decoded, both may be
     * empty, '=' may be omitted */
    size_t pos = 0;

    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        std::string_view pair(query.data() + pos,
                (amp == std::string::npos ? query.size() : amp) - pos);

        if (!pair.empty()) {
            size_t eq = pair.find('=');
            std::string key, value;

            if (!PercentDecode(pair.substr(0, eq), true, key) ||
                    !PercentDecode(eq == std::string_view::npos ?
                    std::string_view() : pair.substr(eq + 1),
                    true, value))
                continue;  /* skip pair with invalid escape */
            query_params.emplace_back(std::move(key), std::move(value));
        }

        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
}

const std::string *http_request_msg::GetQueryParam(
        std::string_view name) const
{
    ParseQueryParams();

    for (const auto &[key, value] : query_params)
        if (key == name)
            return &value;
    return nullptr;
}

bool http_request_msg::WantKeepAlive() const
{
    /* Close connection after response on any error */
    if (IsError())
        return false;

    if (HasHeader("Connection")) {
        const std::string conn_val =
                AsString(GetHeaderValue("Connection"));

        if (contains_ci(conn_val, "close"))
            return false;
        if (contains_ci(conn_val, "keep-alive"))
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

http_request_ptr_t http_request_msg::Create(
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

bool http_request_msg::IsHostMatching() const
{
    return host.substr(0, host.find(':')) == SRV_CONFIG.host_name;
}

std::string http_request_msg::Serialize() const
{
    std::ostringstream out;

    out << method << " " << target << " " << version << HTTP_CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}

http_response_ptr_t http_request_msg::ServeRequest()
{
    if (IsError())
        return make_http_error_response(GetErrorCode(), false);

    return ServeMethodRequest();
}

http_response_ptr_t http_request_msg::ServeMethodRequest()
{
    /* Should not be called for valid requests of base class type */
    return make_http_error_response(
            HTTP_STATUS_INTERNAL_SERVER_ERROR, false);
}
