#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

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
};

std::string guess_mime_type(const std::string &file_path)
{
    size_t dot = file_path.find_last_of('.');

    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string ext = file_path.substr(dot);

    auto it = MIME_TYPES.find(to_lower(ext));
    return it != MIME_TYPES.end() ? it->second :
            std::string("application/octet-stream");
}

std::string make_date_hdr()
{
    std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    std::ostringstream out;

    out << std::put_time(std::gmtime(&now), "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

std::string make_simple_html_page(const std::string &title,
                                const std::string &body)
{
    std::ostringstream out;

    out << "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "    <meta charset=\"UTF-8\">\n"
           "    <title>" << title << "</title>\n"
           "</head>\n"
           "<body>\n" << body << "</body>\n"
           "</html>\n";

    return out.str();
}

std::string make_http_error_msg_body(http_status code,
                                    const std::string &body_text)
{
    std::string code_str = std::to_string(code);
    std::string status_text = http_status_text[code];

    return make_simple_html_page(code_str + " " + status_text,
                            "<h1>" + code_str + " " + status_text +
                            " " + body_text + "</h1>\n");
}

http_response_ptr_t make_http_base_response(http_status code, bool keep_alive,
                                            const std::string &mime_type,
                                            size_t body_size,
                                            const std::string &body)
{
    auto resp = std::make_shared<http_response_msg>(code);

    resp->AddHeader("Date", make_date_hdr());
    resp->AddHeader("Server", "my_srv_async/1.0 (Ubuntu)");
    resp->AddHeader("Connection", keep_alive ? "keep-alive" : "close");
    resp->AddHeader("Content-Type", mime_type);
    resp->AddHeader("Content-Length", body_size);
    resp->SetBody(body);

    if (code == HTTP_STATUS_METHOD_NOT_ALLOWED)
        resp->AddHeader("Allow",
                "GET, HEAD, POST, PUT, DELETE, PATCH");

    return resp;
}

http_response_ptr_t make_http_error_response(http_status code, bool keep_alive,
                                            const std::string &add_text)
{
    std::string body = make_http_error_msg_body(code, add_text);

    return make_http_base_response(code, keep_alive, "text/html; charset=UTF-8",
                add_text.size() ? body.size() : 0,
                add_text.size() ? body : "");
}

http_response_ptr_t make_http_ok_response(bool keep_alive,
                                        const std::string &mime_type,
                                        const std::string &body)
{
    return make_http_base_response(HTTP_STATUS_OK, keep_alive, mime_type,
                                    body.size(), body);
}

http_response_ptr_t make_http_ok_response(bool keep_alive,
                                        const std::string &mime_type,
                                        size_t body_size)
{
    return make_http_base_response(HTTP_STATUS_OK, keep_alive, mime_type,
                                    body_size, "");
}