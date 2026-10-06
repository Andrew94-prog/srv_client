#ifndef SRV_HTTP_MSG_INTERNAL_H
#define SRV_HTTP_MSG_INTERNAL_H

/* Internal helpers shared between http message implementation files.
 * Not part of public server interface */

#include <memory>
#include <string>
#include <string_view>

#include "srv_http_msg.h"

namespace srv_http {

constexpr std::string_view CRLF = "\r\n";

std::string to_lower(std::string_view s);

/* Case-insensitive search of "needle" inside "haystack" */
bool contains_ci(std::string_view haystack, std::string_view needle);

std::string_view trim_spaces(std::string_view s);

using response_ptr_t = std::shared_ptr<http_response_msg>;

/* Fill common headers and create response with html page in body */
response_ptr_t make_page_response(http_status code,
                                  const http_request_msg &req,
                                  std::string_view title,
                                  const std::string &body_html,
                                  bool omit_body);

/* Response with error code for invalid or unhandled requests */
response_ptr_t make_error_response(http_status code,
                                   const http_request_msg &req);

/* Server's main page response. The only difference for HEAD is
 * absence of body in response */
response_ptr_t make_main_page_response(const http_request_msg &req,
                                       bool omit_body);

} // namespace srv_http

#endif /* SRV_HTTP_MSG_INTERNAL_H */
