#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "srv_config.h"
#include "srv_defs.h"
#include "srv_http_msg.h"

namespace {

std::string to_lower(std::string_view s)
{
    std::string result(s);

    for (char &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

/* Case-insensitive search of "needle" inside "haystack" */
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

constexpr std::string_view CRLF = "\r\n";

} // namespace

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

http_msg::header_value_t http_msg::GetHeaderValue(
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
            out << name << ": " << v << CRLF;
        }, value);
    out << CRLF << body;
}

/* ------------------------------------------------------------------ */
/* http_request_msg                                                    */

void http_request_msg::ParseRequestHeader(std::string_view buf)
{
    auto fail = [this]() {
        parse_error = true;
        SetErrorCode(HTTP_STATUS_BAD_REQUEST);
    };

    /* Parse request line: METHOD SP request-target SP HTTP-version CRLF */
    size_t line_end = buf.find(CRLF);
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
    std::string_view p = buf.substr(line_end + CRLF.size());
    while (!p.empty()) {
        line_end = p.find(CRLF);
        if (line_end == std::string_view::npos)
            return fail();

        line = p.substr(0, line_end);
        if (!line.empty()) {
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                return fail();

            std::string_view name = line.substr(0, colon);
            std::string_view value = trim_spaces(line.substr(colon + 1));

            if (to_lower(name) == "content-length") {
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
        p.remove_prefix(std::min(line_end + CRLF.size(), p.size()));
    }
}

bool http_request_msg::WantKeepAlive() const
{
    /* Close connection after response on any error */
    if (IsError())
        return false;

    if (HasHeader("Connection")) {
        const std::string &conn_val =
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

    out << method << " " << target << " " << version << CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}

/* ------------------------------------------------------------------ */
/* Forming of responses                                                */

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

using response_ptr_t = std::shared_ptr<http_response_msg>;

/* Fill common headers and create response with html page in body */
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

/* Response with error code for invalid or unhandled requests */
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

} // namespace

std::shared_ptr<http_response_msg> http_request_msg::MakeResponse()
{
    /* Response for invalid request or request received with error */
    if (IsError()) {
        return make_error_response(GetErrorCode() != HTTP_STATUS_NONE ?
                                   GetErrorCode() : HTTP_STATUS_BAD_REQUEST,
                                   *this);
    }

    /* Dispatch to method-specific response for valid request */
    return MakeOkResponse();
}

std::shared_ptr<http_response_msg> http_request_msg::MakeOkResponse()
{
    /* Should not be called for valid requests of base class type */
    return make_error_response(HTTP_STATUS_INTERNAL_SERVER_ERROR, *this);
}

/* Server's main page plug for proper GET request.
 * The only difference for HEAD is absence of body in response */
static response_ptr_t make_main_page_response(const http_request_msg &req,
                                              bool omit_body)
{
    if (req.target == "/" || req.target == "/index.html") {
        return make_page_response(HTTP_STATUS_OK, req, "Main Page",
                "<h1>Welcome to coroutine-based async server!</h1>\n"
                "<p>This is the server's main page plug.</p>\n",
                omit_body);
    }

    return make_page_response(HTTP_STATUS_NOT_FOUND, req, "Not Found",
            "<h1>404 Page Not Found</h1>\n"
            "<p>Requested page: " + req.target + "</p>\n",
            omit_body);
}

std::shared_ptr<http_response_msg> http_request_get_msg::MakeOkResponse()
{
    return make_main_page_response(*this, false);
}

std::shared_ptr<http_response_msg> http_request_head_msg::MakeOkResponse()
{
    return make_main_page_response(*this, true);
}

/* Plug responses for methods not implemented in the first version.
 * Server answers with "501 Not Implemented" error code */

std::shared_ptr<http_response_msg> http_request_post_msg::MakeOkResponse()
{
    return make_page_response(HTTP_STATUS_NOT_IMPLEMENTED, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of POST requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_put_msg::MakeOkResponse()
{
    return make_page_response(HTTP_STATUS_NOT_IMPLEMENTED, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of PUT requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_delete_msg::MakeOkResponse()
{
    return make_page_response(HTTP_STATUS_NOT_IMPLEMENTED, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of DELETE requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_patch_msg::MakeOkResponse()
{
    return make_page_response(HTTP_STATUS_NOT_IMPLEMENTED, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of PATCH requests will be implemented"
            " in the next version of server.</p>\n", false);
}

/* ------------------------------------------------------------------ */

std::string http_response_msg::Serialize() const
{
    std::ostringstream out;

    out << "HTTP/1.1 " << static_cast<int>(status_code) << " "
        << status_text << CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}
