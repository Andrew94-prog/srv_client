/* Common class of http responses to clients and shared helpers
 * for forming of responses */

#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <chrono>
#include <ctime>

#include "srv_defs.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

std::string http_response_msg::GuessMimeType(const std::string &path)
{
    if (path == "/")
        return MIME_TYPES.at(".html");

    if (path.starts_with("/api/"))
        return MIME_TYPES.at(".json");

    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return MIME_TYPES.at(".bin");

    std::string ext = path.substr(dot);
    auto it = MIME_TYPES.find(to_lower(ext));
    return it != MIME_TYPES.end() ? it->second :
            MIME_TYPES.at(".bin");
}

std::string http_response_msg::MakeDateHdr()
{
    std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    std::ostringstream out;

    out << std::put_time(std::gmtime(&now), "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

std::string http_response_msg::MakeSimpleHtmlPage(const std::string &t,
                                                const std::string &b)
{
    std::ostringstream out;

    out << "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "    <meta charset=\"UTF-8\">\n"
           "    <title>" << t << "</title>\n"
           "</head>\n"
           "<body>\n" << b << "</body>\n"
           "</html>\n";

    return out.str();
}

void http_response_msg::SetBody(const std::string &b)
{
    if (IsOk()) {
        http_msg::SetBody(MakeSimpleHtmlPage(
                std::to_string(status_code) + " " + status_text,
                b));
    } else {
        http_msg::SetBody(b);
    }

    AddHeader("Content-Length", body.size());
}

void http_response_msg::SetBodySize(std::size_t size)
{
    AddHeader("Content-Length", size);
}

http_response_msg::http_response_msg(const http_request_msg &req,
                                    HTTP_STATUS code)
{
    status_code = code;
    status_text = HTTP_STATUS_TEXT[code];

    AddHeader("Date", MakeDateHdr());
    AddHeader("Server", "my_srv_async/1.0 (Ubuntu)");
    AddHeader("Connection", req.WantKeepAlive() ? "keep-alive" : "close");
    AddHeader("Content-Type", GuessMimeType(req.GetPath()));

    if (code == HTTP_STATUS_METHOD_NOT_ALLOWED)
        AddHeader("Allow", "GET, HEAD, POST, PUT, DELETE, PATCH");
    
}

http_response_msg::http_response_msg(const http_request_msg &req,
                        HTTP_STATUS code, const std::string &b) :
                http_response_msg(req, code)
{
    SetBody(b);
}

std::string http_response_msg::Serialize() const
{
    std::ostringstream out;

    out << "HTTP/1.1 " << status_code << " "
        << status_text << HTTP_CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}


/* -------------------------------------------------------------------------- */

response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code)
{
    return std::make_shared<http_response_msg>(req, code);
}

response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code,
                                const std::string &b)
{
    return std::make_shared<http_response_msg>(req, code, b);
}