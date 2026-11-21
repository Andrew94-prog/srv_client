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


const std::string HTTP_CRLF = "\r\n";
const std::vector<std::string> HTTP_STATUS_TEXT = [] {
    std::vector<std::string> text(HTTP_STATUS_MAX);

    text[HTTP_STATUS_OK] = "OK";
    text[HTTP_STATUS_CREATED] = "Created";
    text[HTTP_STATUS_NO_CONTENT] = "No Content";
    text[HTTP_STATUS_BAD_REQUEST] = "Bad Request";
    text[HTTP_STATUS_FORBIDDEN] = "Forbidden";
    text[HTTP_STATUS_NOT_FOUND] = "Not Found";
    text[HTTP_STATUS_METHOD_NOT_ALLOWED] = "Method Not Allowed";
    text[HTTP_STATUS_REQUEST_TIMEOUT] = "Request Timeout";
    text[HTTP_STATUS_CONFLICT] = "Conflict";
    text[HTTP_STATUS_LENGTH_REQUIRED] = "Length Required";
    text[HTTP_STATUS_PAYLOAD_TOO_LARGE] = "Payload Too Large";
    text[HTTP_STATUS_INTERNAL_SERVER_ERROR] = "Internal Server Error";
    text[HTTP_STATUS_NOT_IMPLEMENTED] = "Not Implemented";
    text[HTTP_STATUS_HEADER_FIELDS_TOO_LARGE] =
            "Request Header Fields Too Large";
    return text;
}();

const std::unordered_map<std::string, std::string>
            MIME_TYPES = {
    {".html", "text/html; charset=UTF-8"},
    {".htm",  "text/html; charset=UTF-8"},
    {".css",  "text/css; charset=UTF-8"},
    {".js",   "text/javascript; charset=UTF-8"},
    {".txt",  "text/plain; charset=UTF-8"},
    {".json", "application/json; charset=UTF-8"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".pdf",  "application/pdf"},
    {".bin",  "application/octet-stream"},
};

void http_msg::AddHeader(std::string_view name, std::string_view value)
{
    headers[to_lower(name)] = std::string(value);
}

void http_msg::AddHeader(std::string_view name, size_t value)
{
    headers[to_lower(name)] = value;
}

bool http_msg::HasHeader(std::string_view name) const
{
    return headers.contains(to_lower(name));
}

header_value_t http_msg::GetHeaderValue(
        std::string_view name) const
{
    auto it = headers.find(to_lower(name));

    if (it == headers.end())
        return header_value_t();
    return it->second;
}

void http_msg::SerializeHeadersAndBody(std::ostringstream &out) const
{
    for (const auto &[name, value] : headers)
        std::visit([&out, &name](const auto &v) {
            out << name << ": " << v << HTTP_CRLF;
        }, value);
    out << HTTP_CRLF << body;
}
