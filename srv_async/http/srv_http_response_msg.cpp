/* Common class of http responses to clients and shared helpers
 * for forming of responses */

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"

namespace {

std::string get_html_page(std::string_view title,
                          const std::string &body_html)
{
    std::ostringstream out;

    out << "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "    <meta charset=\"UTF-8\">\n"
           "    <title>" << title << "</title>\n"
           "</head>\n"
           "<body>\n" << body_html << "</body>\n"
           "</html>\n";
    return out.str();
}

std::string format_date_hdr()
{
    std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    std::ostringstream out;

    out << std::put_time(std::gmtime(&now), "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

} // namespace

namespace srv_http {

response_ptr_t make_page_response(http_status code,
                                  const http_request_msg &req,
                                  std::string_view title,
                                  const std::string &body_html,
                                  bool omit_body)
{
    auto resp = std::make_shared<http_response_msg>(code);
    std::string page = get_html_page(title, body_html);

    resp->AddHeader("Date", format_date_hdr());
    resp->AddHeader("Server", "my_srv_async/1.0 (Ubuntu)");
    resp->AddHeader("Connection",
                     req.WantKeepAlive() ? "keep-alive" : "close");
    resp->AddHeader("Content-Type", "text/html; charset=UTF-8");
    /* For HEAD Content-Length is set as if the body was sent */
    resp->AddHeader("Content-Length", page.size());
    if (!omit_body)
        resp->SetBody(std::move(page));

    return resp;
}

response_ptr_t make_error_response(http_status code,
                                   const http_request_msg &req)
{
    std::string code_str = std::to_string(static_cast<int>(code));
    std::string text = http_status_text[static_cast<size_t>(code)];
    response_ptr_t resp = make_page_response(code, req,
            code_str + " " + text,
            "<h1>" + code_str + " " + text + "</h1>\n",
            false);

    if (code == HTTP_STATUS_METHOD_NOT_ALLOWED)
        resp->AddHeader("Allow",
                "GET, HEAD, POST, PUT, DELETE, PATCH");

    return resp;
}

} // namespace srv_http

std::string http_response_msg::Serialize() const
{
    std::ostringstream out;

    out << "HTTP/1.1 " << static_cast<int>(status_code) << " "
        << status_text << srv_http::CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}
