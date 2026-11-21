# Documentation of file-system library (directory `libfs/`)

Helpers for working with files and directories of the server's base
directory: access checks, size queries and block-wise reading/writing of
file content that cooperates with the coroutine scheduler. Implementation
is in `srv_fs.cpp` (uses `std::filesystem` and file streams).

# libfs/srv_fs.h

Interface of the file-system helper library, used by the http layer when
serving static files (HEAD/GET) and, in future versions, when handling
uploads (PUT) and deletions (DELETE). All functions operate on absolute
paths — callers build them as `SRV_CONFIG.base_dir` + request path.

## `bool dir_access_ok(const std::string &dpath)`

Parameters:
- `dpath` — absolute path of the directory to check

Description:
Checks that the path exists, is a directory and can be iterated by this
process: `std::filesystem::exists()` / `is_directory()` plus construction of
a `filesystem::directory_iterator`, all with error codes (no exceptions).
Any failure, including permissions, yields false.

Return value: true if the directory exists and is accessible, false
otherwise.

## `bool file_access_ok(const std::string &fpath)`

Parameters:
- `fpath` — absolute path of the file to check

Description:
Checks with `std::filesystem::exists()` / `is_regular_file()` that the path
is an existing regular file; used by file serving to choose between 200 and
404. Permission problems are not detected here — an unreadable file still
passes and fails later at open time.

Return value: true if the path is an existing regular file, false
otherwise.

## `size_t file_size(const std::string &fpath)`

Parameters:
- `fpath` — absolute path of the file

Description:
Returns the file size via `std::filesystem::file_size()` with an error code;
on any error returns 0 instead of throwing. Used for the `Content-Length`
of HEAD responses; a 0 result is ambiguous with a genuinely empty file,
which is acceptable for serving.

Return value: file size in bytes, or 0 if the size could not be determined.

## `std::string download_file(const std::string &fpath)`

Parameters:
- `fpath` — absolute path of the file to read

Description:
Reads the whole file into a string: opens it in binary mode, determines the
size by seeking to the end, pre-sizes the result and reads sequentially in
blocks of at most `FS_ACCESS_BLOCK_SIZE` (5 MiB), yielding to the scheduler
after each block so other connections keep being served. Used by GET file
serving to build the response body.

Return value: file content as a string, or "" if the file cannot be opened.

## `int upload_file(const std::string &fpath, const std::string &content)`

Parameters:
- `fpath` — absolute path of the file to write
- `content` — full content to write into the file

Description:
Writes the content into the file (created or truncated, binary mode) in
blocks of at most `FS_ACCESS_BLOCK_SIZE`, yielding to the scheduler after
each block — the counterpart of the chunked reading in `download_file()`.
Intended for future PUT request handling.

Return value: 0 on success, -1 if the file cannot be opened for writing.
