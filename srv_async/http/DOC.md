# Documentation of http layer (directory `http/`)

C++20 implementation of http message handling:

- base class of all http messages, classes of requests and responses and
  classes for each supported method (GET, HEAD, POST, PUT, DELETE, PATCH) —
  `srv_http_msg.h` (implementation is split between `srv_http_msg.cpp`,
  `srv_http_request_msg.cpp`, `srv_http_response_msg.cpp` and
  `srv_http_request_<method>.cpp` files)
- receiving and sending of http messages from/to clients —
  `srv_http_send_recv.h` / `srv_http_send_recv.cpp`
- internal string helpers shared by implementation files —
  `srv_http_msg_internal.h` / `srv_http_msg_internal.cpp`

The http layer receives raw data through `recv_from_curr_conn()` /
`send_to_curr_conn()` and cooperates with the coroutine scheduler via
`cond_swap_to_main_ctx()` (see root `DOC.md`).

# http/srv_http_msg.h

Main header of the http layer. Declares the base class of all http messages
(`http_msg`), the common class of client requests (`http_request_msg`),
request classes for each supported method, the response class
(`http_response_msg`) and the factory functions `make_http_response()`.

Also declares shared types and tables: `HTTP_STATUS` (status codes enum),
`HTTP_CRLF`, `HTTP_STATUS_TEXT` (code to reason phrase),
`MIME_TYPES` (extension to Content-Type), `header_value_t`
(`std::variant<std::string, size_t>`) and the `std::shared_ptr` aliases
`request_ptr_t` / `response_ptr_t`.

## class http_msg

Base class of all http messages (requests and responses). Stores header
fields in an unordered map with lower-cased names (case-insensitive storage
and lookup) and values of type `header_value_t`, plus the body string. Full
serialization is defined in derived classes, so `Serialize()` is pure
virtual here.

Protected data members: `headers` (map of header fields) and `body`.

### `http_msg()` — public

Parameters:
- none

Description:
Default constructor: creates an empty message without header fields and with
an empty body. Used as the base subobject of requests/responses; content is
filled later by parsing or by AddHeader/SetBody calls.

Return value: none.

### `virtual ~http_msg()` — public

Parameters:
- none

Description:
Virtual destructor (default implementation). Allows safe destruction of
derived objects through a base-class pointer; `request_ptr_t` /
`response_ptr_t` rely on this.

Return value: none.

### `void AddHeader(std::string_view name, std::string_view value)` — public

Parameters:
- `name` — name of the header field (case-insensitive)
- `value` — value of the header field as a string

Description:
Adds a header field or replaces the existing one with the same name. The
name is converted to lower case before storing, making storage and lookup
case-insensitive. The value is stored as the `std::string` alternative of
`header_value_t`. Used when parsing requests and when forming responses.

Return value: none.

### `void AddHeader(std::string_view name, size_t value)` — public

Parameters:
- `name` — name of the header field (case-insensitive)
- `value` — numeric value of the header field

Description:
Numeric overload of the header setter: same case-insensitive behavior, but
the value is stored as the `size_t` alternative of `header_value_t`. Used
for decimal headers such as `Content-Length`, letting callers read the
number back with `AsSizeT()` without re-parsing.

Return value: none.

### `bool HasHeader(std::string_view name) const` — public

Parameters:
- `name` — name of the header field to look up (case-insensitive)

Description:
Checks whether the message contains a header field with the given name. The
name is lower-cased before the lookup, matching the storage convention of
`AddHeader()`. Typically used for `Connection` in keep-alive logic and
`Content-Length` during parsing.

Return value: true if the header field is present, false otherwise.

### `header_value_t GetHeaderValue(std::string_view name) const` — public

Parameters:
- `name` — name of the header field to read (case-insensitive)

Description:
Returns the stored value of the header field. For a missing field returns a
default-constructed variant (empty string) — use `HasHeader()` to
distinguish absence. Unpack the result with `AsString()` / `AsSizeT()`.

Return value: the header value as `std::variant<std::string, size_t>`.

### `static std::string AsString(const header_value_t &value)` — public, static

Parameters:
- `value` — header value variant to unpack

Description:
Extracts the `std::string` alternative with `std::get`. Always succeeds
because the alternative is chosen when the header is added. Used for
text-valued headers (e.g. `Connection`).

Return value: the header value as `std::string`.

### `static size_t AsSizeT(const header_value_t &value)` — public, static

Parameters:
- `value` — header value variant to unpack

Description:
Extracts the `size_t` alternative with `std::get`. Always succeeds because
numeric validity is checked when the header is added (e.g.
`Content-Length` must be a decimal number).

Return value: the header value as `size_t`.

### `void SetBody(const std::string &b)` — public

Parameters:
- `b` — new content of the message body

Description:
Replaces the whole body with the given string. Note that the response class
overrides this method to add html wrapping and the `Content-Length` header;
this base implementation is a plain assignment. Used when the complete body
is known at once.

Return value: none.

### `void AppendBody(std::string_view chunk)` — public

Parameters:
- `chunk` — next portion of the body to append

Description:
Appends a chunk to the current body without replacing existing data. Used
when a request body is received in parts: the bytes that arrived together
with the header section, then the subsequent reads.

Return value: none.

### `const std::string &GetBody() const` — public

Parameters:
- none

Description:
Gives read access to the current body without copying (constant reference).

Return value: constant reference to the body string.

### `size_t GetBodySize() const` — public

Parameters:
- none

Description:
Returns the current size of the message body in bytes; used for size checks
and the `Content-Length` header.

Return value: body size in bytes.

### `virtual std::string Serialize() const = 0` — public, pure virtual

Parameters:
- none

Description:
Transforms the whole http message into a single wire-format string. Pure
virtual because the start line differs between kinds: a request line for
requests, a status line for responses — derived classes implement
`Serialize()` and reuse `SerializeHeadersAndBody()` for the rest.

Return value: the serialized message as `std::string`.

### `void SerializeHeadersAndBody(std::ostringstream &out) const` — protected

Parameters:
- `out` — output stream to append the data to

Description:
Writes the common message tail to the stream: every stored header field as
"name: value\r\n" (unordered map order; the visitor prints either the string
or the numeric alternative), then the empty line and the body. Called by
derived `Serialize()` implementations after the start line.

Return value: none.

## class http_request_msg : public http_msg

Common class of http requests received from clients. In addition to header
and body storage it holds the request line parts (method, target, version),
the parsed target (normalized path plus raw query string with decoded
parameters), the `Host` header value and the error code. Also implements
the registry of supported http methods (`methods_info`) used by the static
factory `Create()` and by `MethodHasBody()`.

Nested types: `query_t` (one decoded key/value parameter),
`query_iterator_t`, and `method_info_t` — per-method description with a
factory `creator` and a `has_body` flag.

Protected data members: `method`, `target`, `version`, `host`, `path`,
`query_s`, `error_code`, `query_seq` (parameters in order of appearance),
`query_map` (name to last occurrence).

### `http_request_msg()` — public

Parameters:
- none

Description:
Default constructor: error code is HTTP_STATUS_OK, all other fields empty.
Request objects of concrete method classes are created this way by their
factories; afterwards `ParseRequestHeader()` fills in the request line,
headers and parsed target, possibly replacing the error code.

Return value: none.

### `http_request_msg(HTTP_STATUS err_code)` — public

Parameters:
- `err_code` — error code assigned to the request

Description:
Creates a request object marked erroneous from the beginning. Such an object
is not parsed further: `ServeRequest()` sees the error code and immediately
forms the error response. Used for requests that cannot be represented by a
proper method class.

Return value: none.

### `http_request_msg(HTTP_STATUS err_code, std::string_view method)` — public

Parameters:
- `err_code` — error code assigned to the request
- `method` — method name from the request line, stored for later use

Description:
Same as the previous constructor, but also stores the method name so it is
available in the object (logging, error response). In practice created by
`Create()` for a method token that is not among the supported methods.

Return value: none.

### `virtual ~http_request_msg()` — public

Parameters:
- none

Description:
Virtual destructor (default). Request objects are owned via `request_ptr_t`
held through pointers to this common base class, which requires a virtual
destructor for correct destruction of derived method classes.

Return value: none.

### `const std::string &GetMethod() const` — public

Parameters:
- none

Description:
Gives read access to the http method name ("GET", "POST", ...) as spelled
in the request line. Set during parsing or by the error constructor for
unsupported methods; used mostly for logging.

Return value: constant reference to the method name string.

### `const std::string &GetTarget() const` — public

Parameters:
- none

Description:
Gives read access to the raw request target — the part between method and
version in the request line, e.g. "/index.html?a=1". No decoding or
normalization applied; use `GetPath()` / `GetQuery()` for the parsed parts.

Return value: constant reference to the request target string.

### `const std::string &GetVersion() const` — public

Parameters:
- none

Description:
Gives read access to the http version token from the request line, e.g.
"HTTP/1.1". Participates in the keep-alive decision: without a
`Connection` header, only "HTTP/1.0" implies closing the connection.

Return value: constant reference to the version string.

### `const std::string &GetHost() const` — public

Parameters:
- none

Description:
Gives read access to the value of the `Host` header, as received (possibly
with a ":port" suffix). Used by `IsHostMatching()`; empty when the client
sent no `Host` header.

Return value: constant reference to the host string.

### `const std::string &GetPath() const` — public

Parameters:
- none

Description:
Gives read access to the request path — percent-decoded, normalized, without
the query string; always starts with '/'. This is the value used for file
serving and mime type guessing.

Return value: constant reference to the normalized path string.

### `const std::string &GetQuery() const` — public

Parameters:
- none

Description:
Gives read access to the raw query string of the target — everything after
the first '?', without the '?' itself. Decoded parameters are available to
derived classes through `GetQueryParam()`.

Return value: constant reference to the raw query string.

### `HTTP_STATUS GetErrorCode() const` — public

Parameters:
- none

Description:
Returns the error code currently assigned to the request: HTTP_STATUS_OK
while parsing succeeds; 400/411/413 from parsing problems; 405 set by
`Create()` for unsupported methods. Used by `ServeRequest()` to send an
error response instead of serving.

Return value: the error code as `HTTP_STATUS`.

### `bool IsOk() const` — public

Parameters:
- none

Description:
Tells whether the request is error-free (error code is HTTP_STATUS_OK).
Checked after `Create()`, before serving, and inside `WantKeepAlive()`
(connections are closed after error responses).

Return value: true if there is no error, false otherwise.

### `bool WantKeepAlive() const` — public

Parameters:
- none

Description:
Determines whether the client wants to keep the connection open, following
the http/1.1 rules. Any error yields false — the connection is closed after
the error response. With a `Connection` header, a case-insensitive "close"
token yields false and "keep-alive" yields true. Without the header,
everything except "HTTP/1.0" keeps the connection. Drives the serving loop
and the `Connection` header of the response.

Return value: true if the connection should be kept alive.

### `bool IsHostMatching() const` — public

Parameters:
- none

Description:
Checks that the request is addressed to this server: the host part of the
`Host` value (before ':', port ignored) must equal `SRV_CONFIG.host_name`.
The server is not a proxy — requests to another host get plug pages
(GET/HEAD) or 403. A missing `Host` counts as non-matching.

Return value: true if the host matches the configured host name.

### `void ParseRequestHeader(std::string_view buf)` — public

Parameters:
- `buf` — complete header section of the request, including the final "\r\n\r\n"

Description:
Parses the header section of a received request. First the request line
"METHOD SP request-target SP HTTP-version": malformed lines, empty parts or
a version not starting with "HTTP/" set the error to 400. Then the target
is handled by `ParseTarget()`. Remaining lines are parsed as "name: value"
fields: `host` is additionally stored in the `host` member; `content-length`
is accepted only for body-carrying methods, must be a decimal number fully
consumed by `from_chars` (else 400) and not exceed `MAX_HTTP_BODY_SIZE`
(else 413), and is stored via the numeric `AddHeader()`; other fields are
stored as strings. A body-method request without `Content-Length` gets 411.
Parsing stops at the first error, recorded in `error_code`.

Return value: none — the result is in the member fields and the error code.

### `response_ptr_t ServeRequest()` — public

Parameters:
- none

Description:
Top-level serving entry point, called by `handle_one_client_request()`. For
an erroneous request immediately builds an error response with an empty
body; otherwise dispatches to the virtual `ServeMethodRequest()` overridden
by each method class. Always returns a valid response object.

Return value: shared pointer to the formed response message.

### `static request_ptr_t Create(std::string_view method)` — public, static

Parameters:
- `method` — method token extracted from the request line

Description:
Factory of request objects. Looks the method up in `methods_info`: for a
supported method the registered creator returns an object of the concrete
`http_request_<method>_msg` class. For an unknown method it logs the event
and returns a base-class object with HTTP_STATUS_METHOD_NOT_ALLOWED and the
method name stored, so serving produces a proper 405 response. Called by
`recv_http_msg()`.

Return value: request object of the corresponding class, or an error-marked
base-class object.

### `static bool MethodHasBody(std::string_view method)` — public, static

Parameters:
- `method` — method name to check

Description:
Tells from the `methods_info` registry whether the method may carry a body:
POST, PUT and PATCH — true; GET, HEAD and DELETE — false; unknown — false.
Used during parsing (require/accept `Content-Length`, receive the body) and
in the checks of `ParseRequestHeader()`.

Return value: true if the method can carry a body.

### `virtual std::string Serialize() const` — public, virtual

Parameters:
- none

Description:
Serializes the request back into wire format: request line
"method target version\r\n" plus headers and body. Used for logging of
received requests, not sent anywhere.

Return value: the serialized request as `std::string`.

### `void ParseTarget()` — protected

Parameters:
- none (operates on the stored `target` member)

Description:
Validates and splits the stored target. Only origin-form is supported — the
target must start with '/' (else 400). Split at the first '?' into `path`
and raw query string. The path is percent-decoded (400 on an invalid
escape), must not contain control characters, then normalized: "." and empty
segments dropped, ".." removes the previous segment, ".." at root is
rejected with 400 (prevents traversal above the base directory). Finally
`ParseQueryParams()` runs.

Return value: none — results in `path`, `query_s`, or the error code.

### `void ParseQueryParams()` — protected

Parameters:
- none (operates on the stored `query_s` member)

Description:
Splits the query string into pairs at '&' and the first '=' (the value may
be absent; keys and values may be empty). Key and value are percent-decoded
in query mode ('+' means a space); a pair with an invalid escape sequence
is skipped. Pairs are kept in order in `query_seq`, and `query_map[key]`
points at the last occurrence, so lookups return the last value.

Return value: none — results in `query_seq` and `query_map`.

### `bool PercentDecode(std::string_view in, bool plus_as_space, std::string &out)` — protected

Parameters:
- `in` — input string with percent-escapes
- `plus_as_space` — if true, '+' is decoded as a space; if false, kept as '+'
- `out` — output string for the decoded result

Description:
Decodes "%XX" escapes (upper- and lower-case hex digits accepted); a
truncated or non-hexadecimal escape fails the whole decoding. Ordinary
characters pass through; '+' becomes a space in query mode or stays '+'
in path mode. `out` is cleared at the start. Used for the path and query
parameters alike.

Return value: true on success, false if the input contains an invalid
escape.

### `std::string GetQueryParam(const std::string &name) const` — protected

Parameters:
- `name` — name of the query parameter, already percent-decoded

Description:
Looks the parameter up in `query_map`, which points at the last pair with
that name in `query_seq` — later occurrences override earlier ones.
Available to derived request classes for building api handlers.

Return value: decoded value of the parameter or "" if not present.

### `void SetErrorCode(HTTP_STATUS code)` — protected

Parameters:
- `code` — error code to assign to the request

Description:
Inline setter for `error_code`, callable only from the request classes
themselves: the parsing functions record 400/411/413 errors here, and
`Create()` uses the error constructor for 405. Once set, the rest of
parsing/serving reacts via `IsOk()`.

Return value: none.

### `virtual response_ptr_t ServeMethodRequest()` — protected, virtual

Parameters:
- none

Description:
Serving hook invoked by `ServeRequest()` for error-free requests. The base
implementation returns a 500 response: a valid request is always represented
by a derived class overriding this method, so the base version effectively
never runs.

Return value: response for the request (base version: 500).

## class http_request_head_msg : public http_request_msg

Class of HEAD requests: the same target routing as GET, but the response
carries no body — only header fields, including `Content-Length` of the body
that GET would return. Also the base class for `http_request_get_msg`, which
reuses its file and api serving logic.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Routing of a HEAD request. For a foreign host: plug pages — "/" and
"/index.html" get an empty 200 response, anything else gets 404. For this
host: the path "/" is extended to "/index.html", "/api/" paths go to
`ServeApiRequest()`, the rest to `ServeFileRequest()`. The returned response
has no body.

Return value: response without body for the HEAD request.

### `response_ptr_t ServeFileRequest()` — protected

Parameters:
- none (operates on the parsed request path)

Description:
Serves a static file: builds `SRV_CONFIG.base_dir` + path and checks it with
`file_access_ok()` (libfs). For an accessible file returns 200 and announces
the file size via `SetBodySize()` (HEAD semantics — no body is sent);
otherwise returns 404. The GET subclass reuses this method and adds the
actual content.

Return value: ready response (200 with size or 404).

### `response_ptr_t ServeApiRequest()` — protected

Parameters:
- none (operates on the parsed request path)

Description:
Plug handler for requests to the "/api/" part: always returns 501 "Not
Implemented", since the api interface is not implemented yet. GET overrides
it to add an explanatory body. Future api handlers will be dispatched here.

Return value: 501 response.

## class http_request_get_msg : public http_request_head_msg

Class of GET requests: the same routing as HEAD (inherited and reused), but
responses carry a body — the file content or the plug page text.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Routing of a GET request, mirroring the HEAD version but with bodies. For a
foreign host: "/" and "/index.html" get a 200 response with a welcome text,
any other path gets 404 with a body naming the requested page. For this
host: the path "/" is extended to "/index.html", "/api/" paths go to
`ServeApiRequest()`, the rest to `ServeFileRequest()`.

Return value: response with body for the GET request.

### `response_ptr_t ServeFileRequest()` — private

Parameters:
- none (operates on the parsed request path)

Description:
Delegates to the HEAD implementation (access check, status code, announced
size). On 200 reads the whole file with `download_file()` (libfs, chunked
reading yielding to the scheduler) and installs it as the body via
`SetBody()`; for paths under "/storage/" adds "Content-Disposition:
attachment" with the request path as filename, forcing a download. On
failure sets an error text body instead.

Return value: response with the file content as body.

### `response_ptr_t ServeApiRequest()` — private

Parameters:
- none (operates on the parsed request path)

Description:
Takes the 501 response built by the HEAD implementation and adds a body
stating that the API interface is not implemented yet. A placeholder for
future api handlers.

Return value: 501 response with explanatory body.

## class http_request_post_msg : public http_request_msg

Class of POST requests. The method is recognized and parsed by the common
code (including the request body), but serving is not implemented yet.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Plug serving of a POST request. Requests addressed to another host are
rejected with 403 "Forbidden" — the server is not a proxy. Otherwise returns
501 with a body stating that handling of POST requests will be implemented
in the next version. The received body is not used yet.

Return value: 403 or 501 response.

## class http_request_put_msg : public http_request_msg

Class of PUT requests. The method is recognized and parsed by the common
code (including the request body), but serving is not implemented yet.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Plug serving of a PUT request. Requests to another host are rejected with
403 "Forbidden". Otherwise returns 501 with a body stating that handling of
PUT requests will be implemented in the next version (intended use:
uploading files under the base directory).

Return value: 403 or 501 response.

## class http_request_delete_msg : public http_request_msg

Class of DELETE requests. The method is recognized and parsed by the common
code; DELETE requests cannot carry a body.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Plug serving of a DELETE request. Requests to another host are rejected with
403 "Forbidden". Otherwise returns 501 with a body stating that handling of
DELETE requests will be implemented in the next version (intended use:
removing files under the base directory).

Return value: 403 or 501 response.

## class http_request_patch_msg : public http_request_msg

Class of PATCH requests. The method is recognized and parsed by the common
code (including the request body), but serving is not implemented yet.

### `response_ptr_t ServeMethodRequest()` — public, override

Parameters:
- none

Description:
Plug serving of a PATCH request. Requests to another host are rejected with
403 "Forbidden". Otherwise returns 501 with a body stating that handling of
PATCH requests will be implemented in the next version (intended use:
partial modification of resources).

Return value: 403 or 501 response.

## class http_response_msg : public http_msg

Class of http responses sent to clients. Complements the base header/body
storage with a status code and its reason phrase, plus response-specific
helpers: mime type guessing, date header generation and simple html page
construction. A response is always built in the context of a request — the
constructor derives the standard headers from it.

Private data members: `status_code` (the `HTTP_STATUS` value) and
`status_text` (the reason phrase).

### `http_response_msg(const http_request_msg &req, HTTP_STATUS code)` — public

Parameters:
- `req` — the request this response answers
- `code` — http status code of the response

Description:
Main constructor: stores the code and its reason phrase from
`HTTP_STATUS_TEXT`, then forms the standard headers — "Date"
(`MakeDateHdr()`), "Server", "Connection" set by `req.WantKeepAlive()`, and
"Content-Type" guessed from the request path (`GuessMimeType()`). For 405 it
also adds an "Allow" header with all supported methods. The body is added
later with `SetBody()` / `SetBodySize()`.

Return value: none.

### `http_response_msg(const http_request_msg &req, HTTP_STATUS code, const std::string &b)` — public

Parameters:
- `req` — the request this response answers
- `code` — http status code of the response
- `b` — body content of the response

Description:
Convenience constructor delegating to the two-argument version and then
installing the given body via `SetBody()` (which also sets
`Content-Length`). The form used by `make_http_response()` and by most
serving code that answers with a text page.

Return value: none.

### `~http_response_msg()` — public

Parameters:
- none

Description:
Default destructor: nothing beyond the base-class members needs explicit
cleanup (the base destructor is virtual).

Return value: none.

### `HTTP_STATUS GetStatusCode() const` — public

Parameters:
- none

Description:
Returns the status code of the response. Used by logging of sent responses
and by the response itself (`IsOk()` checks it, `SetBody()` chooses the
wrapping mode by it).

Return value: the status code as `HTTP_STATUS`.

### `std::string GetStatusText() const` — public

Parameters:
- none

Description:
Returns the reason phrase of the status code ("OK", "Not Found", ...) taken
from `HTTP_STATUS_TEXT` at construction. Used for logging of sent responses.

Return value: the reason phrase string.

### `void SetBody(const std::string &b)` — public

Parameters:
- `b` — body content to install

Description:
Installs the body: for a successful (200) response the content is embedded
into a simple html page (`MakeSimpleHtmlPage()`, title "200 OK"); for an
error response it is used as is. Afterwards the "Content-Length" header is
set to the final body size, so serialization and the byte count agree.

Return value: none.

### `void SetBodySize(std::size_t size)` — public

Parameters:
- `size` — size of the body in bytes

Description:
Sets only the "Content-Length" header without touching the (empty) body —
how HEAD responses announce the size of the resource a GET would return.

Return value: none.

### `bool IsOk() const` — public

Parameters:
- none

Description:
Tells whether the response is successful (status code is
HTTP_STATUS_OK). Used by `SetBody()` for the wrapping mode and by serving
code deciding whether to attach content to a prepared response.

Return value: true if the status code is 200.

### `std::string Serialize() const` — public, override

Parameters:
- none

Description:
Serializes the response: the status line "HTTP/1.1 <code> <text>\r\n" plus
headers and body via `SerializeHeadersAndBody()`. This string is what
`send_http_msg()` transmits and what is logged.

Return value: the serialized response as `std::string`.

### `std::string GuessMimeType(const std::string &path)` — private

Parameters:
- `path` — normalized request path to guess the type for

Description:
Chooses the "Content-Type" value for the path: "/" — text/html; "/api/…" —
application/json; otherwise the file extension (from the last '.',
lower-cased) is looked up in `MIME_TYPES`, with application/octet-stream as
the fallback for unknown or missing extensions.

Return value: the mime type string.

### `std::string MakeDateHdr()` — private

Parameters:
- none

Description:
Formats the current system time in GMT in the http date format
("Wed, 21 Nov 2026 12:34:56 GMT") with `std::put_time`; a fresh value is
generated for every response.

Return value: the formatted date string.

### `std::string MakeSimpleHtmlPage(const std::string &t, const std::string &b)` — private

Parameters:
- `t` — title of the generated page
- `b` — content placed into the `<body>` of the page

Description:
Builds a minimal valid html document: doctype, head with charset and the
given title, and the given content as the body. Used by `SetBody()` to wrap
successful responses and available for error pages.

Return value: the html page as a string.

## `response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code)` — free function

Parameters:
- `req` — the request this response answers
- `code` — http status code of the response

Description:
Convenience factory: constructs a bodyless response with
`std::make_shared<http_response_msg>(req, code)`; the body may be completed
later with `SetBody()`/`SetBodySize()`. All method classes build their
responses through the two `make_http_response()` overloads.

Return value: shared pointer to the created response.

## `response_ptr_t make_http_response(const http_request_msg &req, HTTP_STATUS code, const std::string &b)` — free function

Parameters:
- `req` — the request this response answers
- `code` — http status code of the response
- `b` — body content of the response

Description:
Convenience factory with a body: constructs a response and installs the
given content via `SetBody()` (which also sets `Content-Length`). Used
whenever a textual page is returned — plug pages, error explanations,
"not implemented" notices.

Return value: shared pointer to the created response.

# http/srv_http_send_recv.h

Receiving and sending of complete http messages from/to the current client,
plus the per-request handling function tying everything together.
Implementation is in `srv_http_send_recv.cpp`; functions run inside a
connection coroutine and use the connection's receive buffer and the raw
I/O helpers of the core.

## `std::shared_ptr<http_request_msg> recv_http_msg()`

Parameters:
- none

Description:
Receives one http request. The header section comes from `recv_http_hdr()`
(an empty result — the client went away, nullptr). A header section larger
than `MAX_HTTP_HDR_SZIE` yields a 431 request object. Otherwise the method
token is extracted, the object created by `Create()` and, if valid, its
headers parsed by `ParseRequestHeader()`. For a valid body-carrying method,
the `Content-Length` bytes of body are taken from the buffer leftover and
then received in portions with `CLIENT_OP_TIMEOUT`, appending each to the
body; a short receive aborts with nullptr.

Return value: request object, or nullptr if the request could not be
received completely.

## `ssize_t send_http_msg(const std::shared_ptr<http_response_msg> &resp)`

Parameters:
- `resp` — response message to send to the client

Description:
Serializes the response into a string once, then transmits it in chunks of
at most `SEND_PACKET_SIZE` bytes with `send_to_curr_conn()` under
`CLIENT_OP_TIMEOUT`, conditionally yielding to the scheduler between chunks
so other connections proceed during large transfers. A short send (timeout
or broken connection) aborts the transmission.

Return value: total number of bytes sent on success, 0 if sending failed.

## `bool handle_one_client_request()`

Parameters:
- none

Description:
Serves exactly one request-response exchange; the connection coroutine runs
this in a loop (http keep-alive). Receives the request with
`recv_http_msg()` and logs it; a failure returns false (connection
considered closed). Forms the response with `ServeRequest()` and sends it
with `send_http_msg()`; a failed send also returns false. Returns false as
well if the client asked to close (or the request had an error); otherwise
the coroutine yields to the scheduler and true is returned.

Return value: true to continue serving the connection, false to close it.

## `static std::string_view recv_http_hdr()` — internal (srv_http_send_recv.cpp)

Parameters:
- none

Description:
Receives the header section of one request into the connection's receive
buffer. Data left in the buffer after the previous request is examined
first (http pipelining); if the "\r\n\r\n" terminator is not yet there, the
leftover is compacted to the buffer start and `recv_from_curr_conn()` is
called in terminator mode without timeout. On success the buffer cursors
advance past the header section and a view of it is returned; an empty view
means the client sent nothing or receiving failed.

Return value: view of the received header section, or an empty view on
failure.

## `size_t find_http_headers_end(std::string_view buf)` — internal, anonymous namespace (srv_http_send_recv.cpp)

Parameters:
- `buf` — receive buffer contents to scan

Description:
Searches `buf` for the end of the http header section — the empty line
"\r\n\r\n" — with a single `find()`. Called by `recv_http_hdr()` for both
buffered and freshly received data.

Return value: offset just past the "\r\n\r\n" sequence, or 0 if not found.

## `std::string_view get_method_token(std::string_view buf)` — internal, anonymous namespace (srv_http_send_recv.cpp)

Parameters:
- `buf` — buffer with the beginning of the request

Description:
Extracts the first token of the request line — the method name — as
everything before the first space. If there is no space, an empty view is
returned, which `Create()` treats as an unsupported method.

Return value: view of the method token, or an empty view if not found.

# http/srv_http_msg_internal.h

Internal helpers shared between the http message implementation files; not
part of the public server interface. Also defines the `BASE_DIR` macro
(short alias for `SRV_CONFIG.base_dir`) and re-exports the `MIME_TYPES`
declaration. Implementation is in `srv_http_msg_internal.cpp`.

## `std::string to_lower(std::string_view s)`

Parameters:
- `s` — input string

Description:
Returns a copy of the input with every character converted to lower case.
Used to normalize header field names (case-insensitive handling) and file
extensions (mime type guessing). The input view is not modified.

Return value: lower-cased copy of the input string.

## `bool contains_ci(std::string_view haystack, std::string_view needle)`

Parameters:
- `haystack` — string to search in
- `needle` — substring to search for

Description:
Case-insensitive substring search: character pairs are compared lower-cased
on both sides. An empty needle or a needle longer than the haystack returns
false. Used to find the "close"/"keep-alive" tokens inside the value of the
`Connection` header.

Return value: true if `needle` occurs in `haystack` ignoring case.

## `std::string_view trim_spaces(std::string_view s)`

Parameters:
- `s` — input view to trim

Description:
Removes leading and trailing space characters by adjusting the view bounds
(`remove_prefix`/`remove_suffix`) — no copying, other whitespace untouched.
Used during request parsing to strip the optional whitespace around header
field values.

Return value: trimmed view into the same character data.
