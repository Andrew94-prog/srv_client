/* Common class of http responses to clients and shared helpers
 * for forming of responses */

#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "srv_defs.h"
#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"


std::string http_response_msg::Serialize() const
{
    std::ostringstream out;

    out << "HTTP/1.1 " << status_code << " "
        << status_text << HTTP_CRLF;
    SerializeHeadersAndBody(out);
    return out.str();
}
