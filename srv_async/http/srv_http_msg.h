#ifndef HTTP_MSG_H
#define HTTP_MSG_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <sstream>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <functional>

class http_request_msg;
class http_response_msg;

enum HTTP_STATUS {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_REQUEST_TIMEOUT = 408,
    HTTP_STATUS_LENGTH_REQUIRED = 411,
    HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_STATUS_MAX = 432
};

extern const std::string HTTP_CRLF;
extern const std::vector<std::string> HTTP_STATUS_TEXT;
extern const std::unordered_map<std::string, std::string> MIME_TYPES;

using header_value_t = std::variant<std::string, size_t>;
using request_ptr_t = std::shared_ptr<http_request_msg>;
using response_ptr_t = std::shared_ptr<http_response_msg>;


class http_msg {
public:
    http_msg() = default;
    virtual ~http_msg() = default;

    void AddHeader(std::string_view name, std::string_view value);
    void AddHeader(std::string_view name, size_t value);

    /* Check if message has header field with such name */
    bool HasHeader(std::string_view name) const;

    header_value_t GetHeaderValue(std::string_view name) const;

    /*
     * Transform value of header field to appropriate type.
     * Transformation always succeeds, because valid checks are
     * done when header fields are added
     */
    static std::string AsString(const header_value_t &value)
        { return std::get<std::string>(value); }
    static size_t AsSizeT(const header_value_t &value)
        { return std::get<size_t>(value); }

    void SetBody(const std::string &b) { body = b; }
    void AppendBody(std::string_view chunk) { body.append(chunk); }
    const std::string &GetBody() const { return body; }
    size_t GetBodySize() const { return body.size(); }

    /* Transform whole http message into string */
    virtual std::string Serialize() const = 0;

protected:
    /* Append all header fields and body to output stream */
    void SerializeHeadersAndBody(std::ostringstream &out) const;

    std::unordered_map<std::string, header_value_t> headers;
    std::string body;
};


class http_request_msg : public http_msg {
public:
    http_request_msg() : error_code(HTTP_STATUS_OK) {};
    virtual ~http_request_msg() = default;

    http_request_msg(HTTP_STATUS err_code) : error_code(err_code) {};
    http_request_msg(HTTP_STATUS err_code, std::string_view method) :
        method(method), error_code(err_code) {};

    const std::string &GetMethod() const { return method; }
    const std::string &GetTarget() const { return target; }
    const std::string &GetVersion() const { return version; }
    const std::string &GetHost() const { return host; }
    const std::string &GetPath() const { return path; }
    const std::string &GetQuery() const { return query; }
    const std::string *GetQueryParam(std::string_view name);
    HTTP_STATUS GetErrorCode() const { return error_code; }
    bool IsOk() const { return error_code == HTTP_STATUS_OK; }
    bool WantKeepAlive() const;
    bool IsHostMatching() const;

    void ParseRequestHeader(std::string_view buf);
    response_ptr_t ServeRequest();

    /* 
     * Description of each supported http method: factory for
     * creation of http_request_<method>_msg object and flag
     * if method can have body in request
     */
    struct method_info_t {
        std::function<std::shared_ptr<http_request_msg>()> creator;
        bool has_body;
    };
    static const std::unordered_map<std::string, method_info_t>
            methods_info;
    static std::shared_ptr<http_request_msg> Create(std::string_view method);
    static bool MethodHasBody(std::string_view method);

    virtual std::string Serialize() const;

protected:
    std::string method;   /* http method of request */
    std::string target;   /* request target, e.g. "/index.html" */
    std::string version;  /* http version, e.g. "HTTP/1.1" */
    std::string host;     /* value of 'host' header field of request */
    std::string path;     /* request target path: percent-decoded,
                             normalized, without query string */
    std::string query;    /* raw query string of request target,
                             without leading '?' */
    HTTP_STATUS error_code;  /* response error code */

    std::vector<std::pair<std::string, std::string>> query_params;
    bool query_parsed;

    void ParseTarget();
    void ParseQueryParams();
    bool PercentDecode(std::string_view in, bool plus_as_space,
                        std::string &out);

    void SetErrorCode(HTTP_STATUS code) { error_code = code; }

    virtual response_ptr_t ServeMethodRequest();
};

/* -------------------------------------------------------------- */
/* Classes for each supported http method                          */

class http_request_head_msg : public http_request_msg {
public:
    virtual response_ptr_t ServeMethodRequest() override;

protected:
    response_ptr_t ServeFileRequest();
    response_ptr_t ServeApiRequest();
};

class http_request_get_msg : public http_request_head_msg {
public:
    response_ptr_t ServeMethodRequest() override;

private:
    response_ptr_t ServeFileRequest();
    response_ptr_t ServeApiRequest();
};

class http_request_post_msg : public http_request_msg {
public:
    response_ptr_t ServeMethodRequest() override;
};

class http_request_put_msg : public http_request_msg {
public:
    response_ptr_t ServeMethodRequest() override;
};

class http_request_delete_msg : public http_request_msg {
public:
    response_ptr_t ServeMethodRequest() override;
};

class http_request_patch_msg : public http_request_msg {
public:
    response_ptr_t ServeMethodRequest() override;
};

/* -------------------------------------------------------------- */

class http_response_msg : public http_msg {
private:
    HTTP_STATUS status_code;
    std::string status_text;

    std::string GuessMimeType(const std::string &file_path);
    std::string MakeDateHdr();
    std::string MakeSimpleHtmlPage(const std::string &t,
                                    const std::string &b);

public:
    http_response_msg(const http_request_msg &req,
                    HTTP_STATUS code);
    http_response_msg(const http_request_msg &req,
                    HTTP_STATUS code, const std::string &body);
    ~http_response_msg() = default;

    HTTP_STATUS GetStatusCode() const {return status_code; };
    std::string GetStatusText() const {return status_text; };

    void SetBody(const std::string &body);
    void SetBodySize(std::size_t size);

    bool IsOk() const {return status_code == HTTP_STATUS_OK; };

    std::string Serialize() const override;
};


response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code);
response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code,
                                const std::string &b);

#endif /* HTTP_MSG_H */
