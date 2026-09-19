#include <stdio.h>
#include <string.h>
#include <time.h>

#include "http_msg.h"

void http_msg::add_header(const std::string &name,
                          const std::string &value)
{
    std::string key;

    /* Header names are case-insensitive, use lowercased
     * name as key in unordered_map for fast lookup */
    for (size_t i = 0; i < name.size(); i++)
        key += tolower(name[i]);

    headers.push_back(std::make_pair(name, value));
    headers_map[key] = --headers.end();
}

const std::string *http_msg::find_header(const std::string &name) const
{
    std::string key;

    for (size_t i = 0; i < name.size(); i++)
        key += tolower(name[i]);

    std::unordered_map<std::string, header_iter_t>::const_iterator it =
            headers_map.find(key);
    if (it == headers_map.end())
        return NULL;

    return &it->second->second;
}

void http_msg::serialize_headers_and_body(std::ostringstream &out) const
{
    header_list_t::const_iterator it;

    for (it = headers.begin(); it != headers.end(); it++)
        out << it->first << ": " << it->second << "\r\n";
    out << "\r\n" << body;
}

/* ------------------------------------------------------------------ */
/* http_request_msg                                                    */

http_msg::parse_result http_request_msg::parse_request(const char *buf,
                                                       size_t len)
{
    const char *p, *line_end, *buf_end = buf + len;
    std::string line;

    /* Parse request line: METHOD SP request-target SP HTTP-version CRLF */
    line_end = (const char *)memchr(buf, '\r', len);
    if (!line_end || line_end + 1 >= buf_end || line_end[1] != '\n') {
        parse_error = true;
        return PARSE_ERROR;
    }
    line.assign(buf, line_end - buf);

    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 == std::string::npos || sp2 == sp1) {
        parse_error = true;
        return PARSE_ERROR;
    }
    method = line.substr(0, sp1);
    target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    version = line.substr(sp2 + 1);
    if (method.empty() || target.empty() ||
            version.compare(0, 5, "HTTP/") != 0) {
        parse_error = true;
        return PARSE_ERROR;
    }

    /* Parse header fields: "field-name: OWS field-value OWS CRLF"
     * until end of buf (end of header section) */
    p = line_end + 2;
    while (p < buf_end) {
        line_end = (const char *)memchr(p, '\r', buf_end - p);
        if (!line_end || line_end[1] != '\n') {
            parse_error = true;
            return PARSE_ERROR;
        }
        if (line_end > p) {
            line.assign(p, line_end - p);
            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                parse_error = true;
                return PARSE_ERROR;
            }
            size_t v_begin = colon + 1;
            while (v_begin < line.size() && line[v_begin] == ' ')
                v_begin++;
            size_t v_end = line.size();
            while (v_end > v_begin && line[v_end - 1] == ' ')
                v_end--;
            add_header(line.substr(0, colon),
                       line.substr(v_begin, v_end - v_begin));
        }
        p = line_end + 2;
    }

    return PARSE_OK;
}

bool http_request_msg::want_keep_alive() const
{
    const std::string *conn_hdr;
    bool is_1_0;

    /* Close connection after response on any error */
    if (parse_error || error_code)
        return false;

    is_1_0 = (version == "HTTP/1.0");
    conn_hdr = find_header("Connection");

    if (conn_hdr) {
        if (strcasestr(conn_hdr->c_str(), "close"))
            return false;
        if (strcasestr(conn_hdr->c_str(), "keep-alive"))
            return true;
    }

    return !is_1_0;
}

std::shared_ptr<http_request_msg> http_request_msg::create(
        const std::string &method)
{
    if (method == "GET")
        return std::make_shared<http_request_get_msg>();
    if (method == "HEAD")
        return std::make_shared<http_request_head_msg>();
    if (method == "POST")
        return std::make_shared<http_request_post_msg>();
    if (method == "PUT")
        return std::make_shared<http_request_put_msg>();
    if (method == "DELETE")
        return std::make_shared<http_request_delete_msg>();
    if (method == "PATCH")
        return std::make_shared<http_request_patch_msg>();

    return std::shared_ptr<http_request_msg>();
}

bool http_request_msg::method_has_body(const std::string &method)
{
    return method == "POST" || method == "PUT" || method == "PATCH";
}

std::string http_request_msg::serialize() const
{
    std::ostringstream out;

    out << method << " " << target << " " << version << "\r\n";
    serialize_headers_and_body(out);
    return out.str();
}

/* ------------------------------------------------------------------ */
/* Forming of responses                                                */

static const char *status_text(int code)
{
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
        default:  return "Unknown";
    }
}

static std::string get_html_page(const std::string &title,
                                 const std::string &body_html)
{
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "    <meta charset=\"UTF-8\">\n"
           "    <title>" + title + "</title>\n"
           "</head>\n"
           "<body>\n" + body_html + "</body>\n"
           "</html>\n";
}

static void format_date_hdr(char *date_buf, size_t buf_size)
{
    time_t now = time(NULL);
    struct tm tm_gmt;

    gmtime_r(&now, &tm_gmt);
    strftime(date_buf, buf_size, "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);
}

typedef std::shared_ptr<http_response_msg> response_ptr_t;

/* Fill common headers and create response with html page in body */
static response_ptr_t make_page_response(int code,
                                         const http_request_msg &req,
                                         const std::string &title,
                                         const std::string &body_html,
                                         bool omit_body)
{
    std::shared_ptr<http_response_msg> resp =
            std::make_shared<http_response_msg>(code, status_text(code));
    std::string page = get_html_page(title, body_html);
    char date_buf[64];

    format_date_hdr(date_buf, sizeof(date_buf));
    resp->add_header("Date", date_buf);
    resp->add_header("Server", "my_srv_async/1.0 (Ubuntu)");
    resp->add_header("Connection",
                     req.want_keep_alive() ? "keep-alive" : "close");
    resp->add_header("Content-Type", "text/html; charset=UTF-8");
    /* For HEAD Content-Length is set as if the body was sent */
    resp->add_header("Content-Length", std::to_string(page.size()));
    if (!omit_body)
        resp->set_body(page);

    return resp;
}

/* Response with error code for invalid or unhandled requests */
static response_ptr_t make_error_response(int code,
                                          const http_request_msg &req)
{
    std::string code_str = std::to_string(code);
    response_ptr_t resp = make_page_response(code, req,
            code_str + " " + status_text(code),
            "<h1>" + code_str + " " + status_text(code) + "</h1>\n",
            false);

    if (code == 405)
        resp->add_header("Allow",
                "GET, HEAD, POST, PUT, DELETE, PATCH");

    return resp;
}

std::shared_ptr<http_response_msg> http_request_msg::make_response()
{
    /* Response for invalid request or request received with error */
    if (parse_error || error_code)
        return make_error_response(error_code ? error_code : 400, *this);

    /* Should not be called for valid requests of base class type */
    return make_error_response(500, *this);
}

/* Server's main page plug for proper GET request.
 * The only difference for HEAD is absence of body in response */
static response_ptr_t make_main_page_response(const http_request_msg &req,
                                              bool omit_body)
{
    if (req.target == "/" || req.target == "/index.html") {
        return make_page_response(200, req, "Main Page",
                "<h1>Welcome to coroutine-based async server!</h1>\n"
                "<p>This is the server's main page plug.</p>\n",
                omit_body);
    }

    return make_page_response(404, req, "Not Found",
            "<h1>404 Page Not Found</h1>\n"
            "<p>Requested page: " + req.target + "</p>\n",
            omit_body);
}

std::shared_ptr<http_response_msg> http_request_get_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_main_page_response(*this, false);
}

std::shared_ptr<http_response_msg> http_request_head_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_main_page_response(*this, true);
}

/* Plug responses for methods not implemented in the first version.
 * Server answers with "501 Not Implemented" error code */

std::shared_ptr<http_response_msg> http_request_post_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_page_response(501, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of POST requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_put_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_page_response(501, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of PUT requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_delete_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_page_response(501, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of DELETE requests will be implemented"
            " in the next version of server.</p>\n", false);
}

std::shared_ptr<http_response_msg> http_request_patch_msg::make_response()
{
    /* For invalid request or request received with error
     * response is formed by base class */
    if (parse_error || error_code)
        return http_request_msg::make_response();

    return make_page_response(501, *this, "Not Implemented",
            "<h1>501 Not Implemented</h1>\n"
            "<p>Handling of PATCH requests will be implemented"
            " in the next version of server.</p>\n", false);
}

/* ------------------------------------------------------------------ */

std::string http_response_msg::serialize() const
{
    std::ostringstream out;

    out << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    serialize_headers_and_body(out);
    return out.str();
}
