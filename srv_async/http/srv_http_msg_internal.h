#ifndef SRV_HTTP_MSG_INTERNAL_H
#define SRV_HTTP_MSG_INTERNAL_H

/* Internal helpers shared between http message implementation files.
 * Not part of public server interface */

#include <memory>
#include <string>
#include <string_view>

#include "srv_http_msg.h"

#define BASE_DIR SRV_CONFIG.base_dir

extern const std::unordered_map<std::string, std::string> MIME_TYPES;


std::string to_lower(std::string_view s);

/* Case-insensitive search of "needle" inside "haystack" */
bool contains_ci(std::string_view haystack, std::string_view needle);

std::string_view trim_spaces(std::string_view s);

/* MIME type of file by its extension. For unknown extension
 * returns "application/octet-stream" */
std::string guess_mime_type(const std::string &file_path);

std::string make_date_hdr();

std::string make_http_error_msg_body(http_status code,
                                    const std::string &body_text);

std::string make_simple_html_page(const std::string &title,
                                const std::string &body);

http_response_ptr_t make_http_base_response(http_status code, bool keep_alive,
                                            const std::string &mime_type,
                                            size_t body_size,
                                            const std::string &body);

http_response_ptr_t make_http_error_response(http_status code, bool keep_alive,
                                            const std::string &add_text = "");

http_response_ptr_t make_http_ok_response(bool keep_alive,
                                        const std::string &mime_type,
                                        const std::string &body = "");

http_response_ptr_t make_http_ok_response(bool keep_alive,
                                        const std::string &mime_type,
                                        size_t body_size = 0);                                       

#endif /* SRV_HTTP_MSG_INTERNAL_H */
