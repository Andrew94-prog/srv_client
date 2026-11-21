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

#endif /* SRV_HTTP_MSG_INTERNAL_H */
