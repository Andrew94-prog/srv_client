#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "srv_http_msg.h"
#include "srv_http_msg_internal.h"


std::string to_lower(std::string_view s)
{
    std::string result(s);

    for (char &c : result)
        c = std::tolower(c);
    return result;
}

bool contains_ci(std::string_view haystack, std::string_view needle)
{
    if (needle.empty() || haystack.size() < needle.size())
        return false;

    for (size_t i = 0; i <= haystack.size() - needle.size(); i++) {
        size_t j = 0;

        while (j < needle.size() &&
                std::tolower(haystack[i + j]) ==
                std::tolower(needle[j]))
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