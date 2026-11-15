#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include "srv_defs.h"
#include "srv_conn_ctxt.h"
#include "srv_fs.h"

using namespace std;

bool dir_access_ok(const string &dpath)
{
    error_code ec;

    if (!filesystem::exists(dpath, ec) ||
            !filesystem::is_directory(dpath, ec))
        return false;

    filesystem::directory_iterator it(dpath, ec);
    if (ec)
        return false;

    return true;
}

bool file_access_ok(const string &fpath)
{
    filesystem::path p(fpath);
    
    return filesystem::exists(p) && filesystem::is_regular_file(p);
}

size_t file_size(const string &fpath)
{
    error_code ec;
    size_t fsize = filesystem::file_size(fpath, ec);

    return !ec ? fsize : 0;
}

string download_file(const string &fpath)
{
    ifstream file(fpath, ios::binary);
    if (!file.is_open())
        return "";

    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0, ios::beg);

    string content;
    content.resize(size);

    for (size_t i = 0; i < size; i += FS_ACCESS_BLOCK_SIZE) {
        file.read(content.data() + i, min(FS_ACCESS_BLOCK_SIZE, size - i));

        cond_swap_to_main_ctx();
    }

    return content;
}

int upload_file(const string &fpath, const string &content)
{
    ofstream file(fpath, ios::binary);
    if (!file.is_open())
        return -1;

    size_t size = content.size();

    for (size_t i = 0; i < size; i += FS_ACCESS_BLOCK_SIZE) {
        file.write(content.data() + i, min(FS_ACCESS_BLOCK_SIZE, size - i));

        cond_swap_to_main_ctx();
    }

    return 0;
}