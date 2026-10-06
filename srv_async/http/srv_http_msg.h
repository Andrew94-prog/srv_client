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

class http_response_msg;

/* All http response status codes used by server.
 * Value of each enumerator is a valid http status code,
 * which is used as index in http_status_text */
enum http_status {
    HTTP_STATUS_NONE = 0,               /* no error, request is valid */
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_REQUEST_TIMEOUT = 408,
    HTTP_STATUS_LENGTH_REQUIRED = 411,
    HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_HEADER_FIELDS_TOO_LARGE = 431,
};

/* Text description for each status code from http_status enum.
 * http_status_text[code] is "" for codes not used by server */
extern const std::vector<std::string> http_status_text;

/*
 * Generic http message: header fields + optional body.
 * Each header field is stored in unordered_map with header name
 * as key (lowercased, names are case-insensitive) and variant of
 * all possible supported value types as value
 */
class http_msg {
public:
    /* All possible types of header field value */
    using header_value_t = std::variant<std::string, size_t>;

    http_msg() = default;
    virtual ~http_msg() = default;

    void AddHeader(std::string_view name, std::string_view value);
    void AddHeader(std::string_view name, size_t value);

    /* Check if message has header field with such name */
    bool HasHeader(std::string_view name) const;

    /* Returns value of header field. If there is no header with
     * such name, returns empty value; use HasHeader() to check
     * for existence beforehand */
    header_value_t GetHeaderValue(std::string_view name) const;

    /* Transform value of header field to appropriate type.
     * Transformation always succeeds, because valid checks are
     * done when header fields are added */
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

/*
 * Generic http request from client. Base class for
 * http_request_<method>_msg classes for each supported method
 */
class http_request_msg : public http_msg {
public:
    std::string target;
    std::string version;   /* e.g. "HTTP/1.1" */

    bool parse_error = false;  /* request was received, but malformed */

    http_request_msg() = default;

    /* Create empty request with error code, for requests which
     * could not be received or parsed and no other fields matter */
    explicit http_request_msg(http_status error) :
        error_code(error) {}

    /* Create request with given error code and method, e.g. for
     * requests with unsupported method */
    http_request_msg(http_status error, std::string_view method) :
        method(method), error_code(error) {}

    const std::string &GetMethod() const { return method; }

    void SetErrorCode(http_status code) { error_code = code; }
    http_status GetErrorCode() const { return error_code; }

    /* Request is error if it was malformed or received with error */
    bool IsError() const { return parse_error || error_code != HTTP_STATUS_NONE; }
    bool IsOk() const { return !IsError(); }

    /* Parse request line and header fields of message
     * (body is not parsed, buf must contain whole header section).
     * On malformed message sets error_code to HTTP_STATUS_BAD_REQUEST */
    void ParseRequestHeader(std::string_view buf);

    /* Keep-alive logic: HTTP/1.1 is persistent by default,
     * HTTP/1.0 is not, "Connection" header overrides default */
    bool WantKeepAlive() const;

    /* Form response message for this request */
    virtual std::shared_ptr<http_response_msg> MakeResponse();

    virtual std::string Serialize() const;

    /* Description of each supported http method: factory for
     * creation of http_request_<method>_msg object and flag
     * if method can have body in request */
    struct method_info_t {
        std::function<std::shared_ptr<http_request_msg>()> creator;
        bool has_body;
    };

    static const std::unordered_map<std::string, method_info_t>
            methods_info;

    /* Factory: create http_request_<method>_msg object for supported
     * method or request with HTTP_STATUS_METHOD_NOT_ALLOWED error
     * for unsupported method */
    static std::shared_ptr<http_request_msg> Create(std::string_view method);

    /* Methods which can have body in request */
    static bool MethodHasBody(std::string_view method);

private:
    std::string method;   /* http method of request */

    http_status error_code = HTTP_STATUS_NONE;  /* response error code
                                                    for this request,
                                                    HTTP_STATUS_NONE if
                                                    request is valid */

protected:
    /* Hook for derived classes: form response for valid request */
    virtual std::shared_ptr<http_response_msg> MakeOkResponse();
};

/* -------------------------------------------------------------- */
/* Classes for each supported http method                          */

class http_request_get_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

class http_request_head_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

class http_request_post_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

class http_request_put_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

class http_request_delete_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

class http_request_patch_msg : public http_request_msg {
public:
    std::shared_ptr<http_response_msg> MakeOkResponse() override;
};

/* -------------------------------------------------------------- */

/* The only type for http response to client at least now */
class http_response_msg : public http_msg {
public:
    http_status status_code = HTTP_STATUS_OK;
    std::string status_text = "OK";

    http_response_msg() = default;
    http_response_msg(http_status code) :
        status_code(code), status_text(http_status_text[static_cast<size_t>(code)]) {}

    std::string Serialize() const override;
};

#endif /* HTTP_MSG_H */
