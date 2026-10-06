/* Base class of all http messages: header fields storage
 * and serialization */

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

namespace srv_http {

std::string to_lower(std::string_view s)
{
    std::string result(s);

    for (char &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

bool contains_ci(std::string_view haystack, std::string_view needle)
{
    if (needle.empty() || haystack.size() < needle.size())
        return false;

    for (size_t i = 0; i <= haystack.size() - needle.size(); i++) {
        size_t j = 0;

        while (j < needle.size() &&
               std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
               std::tolower(static_cast<unsigned char>(needle[j])))
            j++;
        if (j == needle.size())
            return true;
    }

    return false;
}

std::string_view trim_spaces(std::string_view s)
{
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    return s;
}

} // namespace srv_http

/* Text description of http status codes, indexed by code value.
 * Size is greater than the maximum used code, so that any
 * http_status value is a valid index; unused codes hold "" */
const std::vector<std::string> http_status_text = [] {
    std::vector<std::string> text(HTTP_STATUS_HEADER_FIELDS_TOO_LARGE + 1);

    text[HTTP_STATUS_OK] = "OK";
    text[HTTP_STATUS_BAD_REQUEST] = "Bad Request";
    text[HTTP_STATUS_NOT_FOUND] = "Not Found";
    text[HTTP_STATUS_METHOD_NOT_ALLOWED] = "Method Not Allowed";
    text[HTTP_STATUS_REQUEST_TIMEOUT] = "Request Timeout";
    text[HTTP_STATUS_LENGTH_REQUIRED] = "Length Required";
    text[HTTP_STATUS_PAYLOAD_TOO_LARGE] = "Payload Too Large";
    text[HTTP_STATUS_INTERNAL_SERVER_ERROR] = "Internal Server Error";
    text[HTTP_STATUS_NOT_IMPLEMENTED] = "Not Implemented";
    text[HTTP_STATUS_HEADER_FIELDS_TOO_LARGE] =
            "Request Header Fields Too Large";
    return text;
}();

void http_msg::AddHeader(std::string_view name, std::string_view value)
{
    /* Header names are case-insensitive, use lowercased
     * name as key in unordered_map for fast lookup */
    headers[srv_http::to_lower(name)] = std::string(value);
}

void http_msg::AddHeader(std::string_view name, size_t value)
{
    headers[srv_http::to_lower(name)] = value;
}

bool http_msg::HasHeader(std::string_view name) const
{
    return headers.contains(srv_http::to_lower(name));
}

http_msg::header_value_t http_msg::GetHeaderValue(
        std::string_view name) const
{
    auto it = headers.find(srv_http::to_lower(name));

    if (it == headers.end())
        return header_value_t();
    return it->second;
}

void http_msg::SerializeHeadersAndBody(std::ostringstream &out) const
{
    for (const auto &[name, value] : headers)
        std::visit([&out, &name](const auto &v) {
            out << name << ": " << v << srv_http::CRLF;
        }, value);
    out << srv_http::CRLF << body;
}
