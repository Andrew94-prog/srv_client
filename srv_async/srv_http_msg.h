#ifndef HTTP_MSG_H
#define HTTP_MSG_H

#include <string>
#include <list>
#include <unordered_map>
#include <sstream>
#include <memory>
#include <utility>

class http_response_msg;

/*
 * Generic http message: header fields + optional body.
 * Header fields are stored in list (fast sequential iteration)
 * and iterators to them in unordered_map (fast access by name)
 */
class http_msg {
public:
    typedef std::pair<std::string, std::string> header_field_t;
    typedef std::list<header_field_t> header_list_t;
    typedef header_list_t::iterator header_iter_t;

    enum parse_result {
        PARSE_OK = 0,
        PARSE_INCOMPLETE,   /* need more bytes to complete message */
        PARSE_ERROR,        /* malformed message */
    };

    http_msg() {}
    virtual ~http_msg() {}

    void add_header(const std::string &name, const std::string &value);
    /* Returns NULL if there is no header with such name */
    const std::string *find_header(const std::string &name) const;

    void set_body(const std::string &b) { body = b; }
    void append_body(const char *buf, size_t len) { body.append(buf, len); }
    const std::string &get_body() const { return body; }
    size_t get_body_size() const { return body.size(); }

    /* Transform whole http message into string */
    virtual std::string serialize() const = 0;

protected:
    /* Append all header fields and body to output stream */
    void serialize_headers_and_body(std::ostringstream &out) const;

    header_list_t headers;
    std::unordered_map<std::string, header_iter_t> headers_map;
    std::string body;
};

/*
 * Generic http request from client. Base class for
 * http_request_<method>_msg classes for each supported method
 */
class http_request_msg : public http_msg {
public:
    std::string method;
    std::string target;
    std::string version;   /* e.g. "HTTP/1.1" */

    int error_code;        /* response error code for this request,
                              0 if request is valid */
    bool parse_error;      /* request was received, but malformed */

    http_request_msg() : error_code(0), parse_error(false) {}

    /* Parse request line and header fields of message
     * (body is not parsed, buf must contain whole header section) */
    parse_result parse_request(const char *buf, size_t len);

    /* Keep-alive logic: HTTP/1.1 is persistent by default,
     * HTTP/1.0 is not, "Connection" header overrides default */
    bool want_keep_alive() const;

    /* Form response message for this request */
    virtual std::shared_ptr<http_response_msg> make_response();

    virtual std::string serialize() const;

    /* Factory: create http_request_<method>_msg object
     * for supported method. Returns NULL for unsupported method */
    static std::shared_ptr<http_request_msg> create(const std::string &method);

    /* Methods which can have body in request */
    static bool method_has_body(const std::string &method);
};

/* -------------------------------------------------------------- */
/* Classes for each supported http method                          */

class http_request_get_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

class http_request_head_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

class http_request_post_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

class http_request_put_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

class http_request_delete_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

class http_request_patch_msg : public http_request_msg {
public:
    virtual std::shared_ptr<http_response_msg> make_response();
};

/* -------------------------------------------------------------- */

/* The only type for http response to client at least now */
class http_response_msg : public http_msg {
public:
    int status_code;
    std::string status_text;

    http_response_msg() : status_code(200), status_text("OK") {}
    http_response_msg(int code, const std::string &text) :
        status_code(code), status_text(text) {}

    virtual std::string serialize() const;
};

#endif /* HTTP_MSG_H */
