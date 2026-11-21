#include <string>

bool dir_access_ok(const std::string &dpath);
bool file_access_ok(const std::string &fpath);
size_t file_size(const std::string &fpath);
std::string download_file(const std::string &fpath);
int upload_file(const std::string &fpath, const std::string &content);