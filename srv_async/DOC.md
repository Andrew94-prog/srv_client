# Documentation of server core (directory `srv_async/`)

Core part of the coroutine-based asynchronous http server, written in pure C:

- parsing of the configuration file — `srv_config.h` / `srv_config.c`
- preparation of listening and client sockets — `srv_sock.h` / `srv_sock.c`
- main working loop of each worker process and coroutine entry function —
  `srv_routines.h` / `srv_routines.c`
- coroutine context management and scheduling — `srv_conn_ctxt.h` / `srv_conn_ctxt.c`
- receiving/sending of raw data from/to clients —
  `srv_conn_send_recv.h` / `srv_conn_send_recv.c`
- common definitions and logging macro — `srv_defs.h`
- intrusive doubly-linked list — `srv_qlist.h`

Http message handling (C++ part) is documented in `http/DOC.md`,
database and file-system helper libraries — in `libdb/DOC.md` and
`libfs/DOC.md`.

# srv_config.h

Parsing of the server configuration file `srv.config` and validation of the
server base directory. Implementation is in `srv_config.c`. The header also
declares the configuration structure `srv_config_t` and the global variable
`SRV_CONFIG` used by all other server modules.

Recognized parameters: `port`, `num_workers`, `log_file`, `log_level`,
`base_dir`, `host_name`, `db_socket`, `db_name`, `db_user`, `db_password`
(see `HELP_MSG` for the expected line format).

## `int parse_srv_config(void)`

Parameters:
- none

Description:
Opens the config file `srv.config` and reads it line by line. Each line is
handled by `parse_srv_config_line()`, which updates the corresponding field
of the global `SRV_CONFIG`. Empty lines, lines without ':' and comment lines
starting with '#' are skipped; unknown keys are silently ignored. Reading
stops on the first failed line and its error code is returned. Called once
from `main()` at startup.

Return value: 0 on success, negative errno code on failure (-ENOENT if the
config file cannot be opened).

## `int validate_srv_base_dir(const char *path)`

Parameters:
- `path` — absolute path to the server base directory to validate

Description:
Checks with `access()` that the base directory is accessible for both read
and write — the http layer serves files from it and stores uploads there. On
failure prints the path and `strerror(errno)` to stderr. Called from
`main()` after config parsing.

Return value: 0 on success, -EACCES if the path is not accessible.

## `static int parse_range_value(const char *val_str, int *val, int min, int max, const char *p_name)` — internal (srv_config.c)

Parameters:
- `val_str` — string with a decimal number from the config file
- `val` — output pointer where the parsed number is stored
- `min` — minimal allowed value of the parameter (inclusive)
- `max` — maximal allowed value of the parameter (inclusive)
- `p_name` — name of the parameter, used in the error message

Description:
Converts `val_str` to an integer with `atoi()` and checks that the result
lies in [min, max]. Used for bounded numeric parameters: `port` (1-65535),
`num_workers`, `log_level`. On an out-of-range value prints an error with
the parameter name and allowed range and leaves `*val` untouched.

Return value: 0 on success, -ERANGE if the value is out of range.

## `static int parse_log_file_name(const char *val_str)` — internal (srv_config.c)

Parameters:
- `val_str` — path to the log file from the config file

Description:
Handles the `log_file` parameter: stores a copy of `val_str` in
`SRV_CONFIG.log_file_name`, opens the file for writing and stores the
`FILE *` in `SRV_CONFIG.log_file_desc`. After that all `LOG()` outputs go to
this file. On failure the previous log destination stays intact.

Return value: 0 on success, -ENOMEM or negative errno on failure.

## `static int parse_string_value(const char *val_str, char **dst, const char *p_name)` — internal (srv_config.c)

Parameters:
- `val_str` — string value of the parameter from the config file
- `dst` — destination field of `SRV_CONFIG` where the copy is stored
- `p_name` — name of the parameter, used in the error message

Description:
Stores a copy of `val_str` in `*dst`, freeing the previous value first, so a
parameter repeated in the config file overrides its earlier value. Used for
`host_name`, `db_socket`, `db_name`, `db_user`, `db_password`. On allocation
failure the old value is untouched.

Return value: 0 on success, -ENOMEM if the copy cannot be allocated.

## `static int parse_base_dir(const char *val_str)` — internal (srv_config.c)

Parameters:
- `val_str` — value of the `base_dir` parameter, relative or absolute path

Description:
Handles the `base_dir` parameter: a relative path is converted to an
absolute one by prepending the current working directory; an absolute path
is stored as is. The result replaces `SRV_CONFIG.base_dir` (old value
freed), so repeated `base_dir` lines override each other.

Return value: 0 on success, -errno if `getcwd()` fails, -ENOMEM on
allocation failure.

## `static char *trim_spaces(char *s)` — internal (srv_config.c)

Parameters:
- `s` — one line of the config file; modified in place

Description:
Removes all whitespace characters (including inner ones) from the string by
compacting non-space characters to the beginning; stops at '\0' or '\n'.
Applied to every config line before splitting it into key and value, making
parsing tolerant to arbitrary spacing around ':'.

Return value: pointer to the same string `s` with whitespace removed.

## `static int parse_srv_config_line(char *line)` — internal (srv_config.c)

Parameters:
- `line` — one line of the config file; modified in place

Description:
Parses a single config line: trims whitespace, skips empty and comment
lines, splits the rest at the first ':' into key and value (lines without
':' or with empty value are ignored). Dispatches known keys to helpers:
`port`/`num_workers`/`log_level` to `parse_range_value()`, `log_file` to
`parse_log_file_name()`, `base_dir` to `parse_base_dir()` and the string
db/hostname parameters to `parse_string_value()`. Unknown keys are ignored
without an error.

Return value: 0 on success or when the line is skipped, negative error code
from the helper on failure.

# srv_sock.h

Helpers for creation and setup of server sockets. Implementation is in
`srv_sock.c`.

## `int create_listening_socket(int srv_port)`

Parameters:
- `srv_port` — TCP port number to listen on

Description:
Creates the listening TCP/IPv4 socket: `socket()`, sets SO_REUSEADDR and
SO_REUSEPORT (immediate port reuse after restart, sharing between forked
workers) and binds to INADDR_ANY:`srv_port`. The socket is intentionally not
put into listening state here — `listen()` is called by each worker in
`handle_connections_routine()`.

Return value: descriptor of the created socket, or -1 on failure.

## `int set_nonblock(int sock)`

Parameters:
- `sock` — descriptor of the socket to configure

Description:
Prepares a socket for asynchronous I/O: sets O_NONBLOCK (read/write return
EAGAIN instead of blocking), F_SETSIG to SIGIO (readiness notifications as
the SIGIO signal) and F_SETOWN to the current thread. Used for the listening
socket and accepted client sockets. On failure the socket is closed by the
function itself.

Return value: 0 on success, -1 on failure (socket is closed).

## `int set_async(int sock)`

Parameters:
- `sock` — descriptor of the socket to configure

Description:
Sets the O_ASYNC flag, enabling SIGIO signals on I/O readiness events —
together with `set_nonblock()` this forms the signal-driven I/O model of the
server. Called for accepted client sockets. On failure the socket is closed
by the function itself.

Return value: 0 on success, -1 on failure (socket is closed).

# srv_routines.h

Main working routines of server worker processes. Implementation is in
`srv_routines.c`.

## `void handle_one_connection(void)`

Parameters:
- none

Description:
Entry function of a connection coroutine. Switches the client socket to
non-blocking async mode, then calls `handle_one_client_request()` in a loop —
one http request is received, served and answered per iteration (http
keep-alive). The loop ends when the client closes the connection, requests
"Connection: close" or a timeout occurs. Then the handled-connections
counter is incremented, the socket is closed (`curr_conn_close()`) and
control returns to the scheduler, which frees the connection.

Return value: none (coroutine ends when connection is served).

## `void handle_connections_routine(int srv_sock)`

Parameters:
- `srv_sock` — descriptor of the listening server socket

Description:
Main function of every worker process (each forked child runs exactly this).
Blocks SIGIO, installs its own SIGINT handler, opens the database connection
(`open_srv_db()`), initializes the connection queue and context cache, and
configures the listening socket (non-blocking mode, epoll registration,
`listen()`). Then runs the infinite scheduling loop: waits for incoming
connections with `epoll_pwait()` (timeout 0 while there are active
connections, -1 otherwise), accepts a new connection and creates a coroutine
for it (`create_new_conn()`), then iterates the whole connection queue
resuming each coroutine (`swap_to_conn_ctx()`) and freeing completed ones.
Once per second prints the number of connections handled in the last second.

Return value: none (function never returns; errors terminate the process).

## `static void sigint_handler(int sig)` — internal (srv_routines.c)

Parameters:
- `sig` — number of the received signal (SIGINT)

Description:
SIGINT handler of worker processes, installed after fork. It replaces the
parent's handler: flushes buffered output to the log file and exits with
code 128 + sig, so the parent's `wait_for_workers()` reports the termination
in a standard way.

Return value: none (the process exits).

# srv_conn_ctxt.h

Coroutine context management: creation and deletion of coroutines with a
context for each client connection and scheduling of coroutines.
Implementation is in `srv_conn_ctxt.c`. Every client connection is served by
its own coroutine (ucontext) with a dedicated stack; all coroutines of a
worker are cooperatively scheduled from
`handle_connections_routine()`.

Declared structures:
- `conn_t` — one client connection: ucontext, memory buffer description
  (receive buffer + guard page + coroutine stack, see the layout comment in
  the header), client socket, activity/timeout timestamps, state flags and
  queue linkage
- `conn_queue_t` — per-worker queue of connections with the currently
  running connection, saved scheduler (main) context, active/inactive
  counters and last context switch time
- `conn_ctx_cache_t` — cache of pre-allocated connection contexts

## `void add_conn_to_queue(conn_t *conn)`

Parameters:
- `conn` — connection to add to the queue

Description:
Inserts the connection at the head of the worker's connection queue and
increments the active or inactive counter according to `conn->is_active`;
the counters drive the epoll timeout choice in the scheduler. Called from
`create_new_conn()`.

Return value: none.

## `void remove_conn_from_queue(conn_t *conn)`

Parameters:
- `conn` — connection to remove from the queue

Description:
Removes the connection from the queue list and decrements the corresponding
active/inactive counter, keeping counters consistent with the list. Called
by the scheduler for every finished connection (`is_completed`).

Return value: none.

## `void init_conn_queue(void)`

Parameters:
- none

Description:
Initializes the per-worker connection queue: empty list, no current
connection, zero active/inactive counters. Must be called once in every
worker before creating connections — done at the start of
`handle_connections_routine()`.

Return value: none.

## `void init_conn_ctx_cache(void)`

Parameters:
- none

Description:
Initializes the connection context cache and fills it up to
`MIN_CONN_CTX_CACHE_CNT` entries. Thanks to the cache, accepting a
connection normally requires no `mmap()`/`malloc()`. Called once per worker
at startup.

Return value: none.

## `void curr_conn_close(void)`

Parameters:
- none

Description:
Closes the client socket of the current connection (unless already closed)
and marks the connection completed; the scheduler then removes it from the
queue and returns its context to the cache. Idempotent. Called by the
coroutine on normal end and by recv/send helpers on fatal errors.

Return value: none.

## `void curr_conn_set_inactive(void)`

Parameters:
- none

Description:
Moves the current connection from active to inactive state, updating queue
counters. Inactive connections are waiting for network data; when no active
connections remain, the scheduler may block in `epoll_pwait()` instead of
spinning. Called by recv/send helpers on prolonged EAGAIN.

Return value: none.

## `bool curr_conn_active_timeout(void)`

Parameters:
- none

Description:
Checks whether `MAX_ACTIVE_TIMEOUT` ns passed since the connection's last
activity (`last_active`, refreshed by `curr_conn_update_active()`). The
recv/send helpers use it on every EAGAIN to decide whether to mark the
connection inactive.

Return value: true if the active timeout is exceeded, false otherwise.

## `bool curr_conn_op_timeout(time_t timeout)`

Parameters:
- `timeout` — allowed duration of the current operation, in nanoseconds

Description:
Checks whether the current network operation, started by
`curr_conn_start_op()`, lasts longer than `timeout` ns (CLOCK_MONOTONIC).
Used by the recv/send helpers to bound one operation with
`CLIENT_OP_TIMEOUT`.

Return value: true if the operation exceeded the timeout, false otherwise.

## `bool need_ctx_swap(void)`

Parameters:
- none

Description:
Checks whether more than `CTX_SWAP_TIMEOUT` ns passed since the last context
switch; used by `cond_swap_to_main_ctx()` to decide whether a long CPU-bound
loop inside a coroutine should yield control to the scheduler.

Return value: true if a context switch to the scheduler is due.

## `void curr_conn_update_active(void)`

Parameters:
- none

Description:
Refreshes `last_active` of the current connection with the current monotonic
time and, if it was inactive, moves it back to active state (updating
counters). Called by recv/send helpers before an operation and after every
successful read/write.

Return value: none.

## `void curr_conn_start_op(void)`

Parameters:
- none

Description:
Records the current monotonic time as the start of a new network operation
in `start_op`; subsequent `curr_conn_op_timeout()` calls measure against it.
Called by recv/send helpers at the beginning of each operation.

Return value: none.

## `void swap_to_main_ctx(void)`

Parameters:
- none

Description:
Yields control from the connection coroutine to the scheduler: saves the
coroutine context and switches to the worker's main context
(`swapcontext()`). When resumed later, execution continues after the swap
and `last_ctx_swap` is refreshed. Called on would-block I/O and by
`cond_swap_to_main_ctx()`; a `swapcontext()` failure terminates the process.

Return value: none.

## `void cond_swap_to_main_ctx(void)`

Parameters:
- none

Description:
Yields to the scheduler only if `need_ctx_swap()` reports that
`CTX_SWAP_TIMEOUT` ns passed since the last switch. Used inside long
non-socket loops (chunked file and message transfers) so that connections
share the worker.

Return value: none.

## `void swap_to_conn_ctx(conn_t *conn)`

Parameters:
- `conn` — connection whose coroutine should be resumed

Description:
Resumes the coroutine of `conn`: makes it the current connection, saves the
scheduler context and switches to it. Returns when the coroutine yields or
finishes; afterwards `last_ctx_swap` is refreshed. Called from the scheduler
loop; a `swapcontext()` failure terminates the process.

Return value: none.

## `void free_closed_conn(conn_t *conn)`

Parameters:
- `conn` — completed connection whose resources should be released

Description:
Returns the finished connection's context to the free pool via
`free_conn_ctx()` — into the cache for reuse, or unmapped when the cache is
full. Called by the scheduler after removing the connection from the queue.

Return value: none.

## `int create_new_conn(int conn_sock)`

Parameters:
- `conn_sock` — descriptor of the accepted client socket

Description:
Creates a coroutine for an accepted socket: takes a context from the cache
(`alloc_conn_ctx()`), initializes the ucontext (stack bounds from the
context buffer, entry `handle_one_connection()`, `uc_link` to the scheduler
main context), resets the state and adds the connection to the queue. The
coroutine is actually started by the scheduler loop on its next iteration.

Return value: 0 on success, -1 if the context cannot be allocated.

## `conn_queue_t *conn_queue(void)`

Parameters:
- none

Description:
Accessor for the per-worker connection queue singleton; gives the scheduler
read access to the queue list and counters without exposing the global
variable directly.

Return value: pointer to the worker's connection queue structure.

## `conn_t *curr_conn(void)`

Parameters:
- none

Description:
Accessor for the currently running connection (last resumed by
`swap_to_conn_ctx()`). The http layer uses it to access the connection's
receive buffer when receiving messages.

Return value: pointer to the current `conn_t`, NULL if none is running.

## `int curr_conn_sock(void)`

Parameters:
- none

Description:
Returns the client socket descriptor of the current connection; used by the
raw recv/send helpers to read from and write to the right socket.

Return value: socket descriptor of the current connection.

## `static time_t curr_time(void)` — internal (srv_conn_ctxt.c)

Parameters:
- none

Description:
Reads the CLOCK_MONOTONIC clock and returns it as a single `time_t` in
nanoseconds. Monotonic time is used for all server timeouts so that system
clock adjustments cannot affect scheduling.

Return value: current monotonic time in nanoseconds.

## `static conn_t *alloc_conn_ctx_mem(void)` — internal (srv_conn_ctxt.c)

Parameters:
- none

Description:
Allocates memory for one connection with `mmap()`: receive buffer, guard
page and coroutine stack in a single mapping. The guard page is protected
with `mprotect(PROT_NONE)`, so a stack overflow causes SIGSEGV instead of
corrupting the receive buffer. Then allocates and fills the `conn_t` with
buffer pointers/sizes and stack bounds.

Return value: initialized `conn_t`, or NULL if `mmap()`, `mprotect()` or
`malloc()` failed.

## `static void free_conn_ctx_mem(conn_t *conn)` — internal (srv_conn_ctxt.c)

Parameters:
- `conn` — connection whose memory should be fully released

Description:
Releases all memory of a connection to the system: `munmap()` of the context
buffer and `free()` of the `conn_t` structure. Used only when the cache
cannot take the connection back.

Return value: none.

## `static void fill_conn_ctx_cache(void)` — internal (srv_conn_ctxt.c)

Parameters:
- none

Description:
Allocates contexts with `alloc_conn_ctx_mem()` and adds them to the cache
until it holds `MIN_CONN_CTX_CACHE_CNT` entries; logs an error if the cache
stays partially filled. Called at init and when the cache is exhausted.

Return value: none.

## `static conn_t *alloc_conn_ctx(void)` — internal (srv_conn_ctxt.c)

Parameters:
- none

Description:
Fast allocation for a new connection: takes an entry from the cache
(refilling it first if empty), avoiding `mmap()`/`mprotect()` on the accept
path.

Return value: `conn_t` from the cache, or NULL if the cache could not be
filled.

## `static void free_conn_ctx(conn_t *conn)` — internal (srv_conn_ctxt.c)

Parameters:
- `conn` — connection to return to the pool

Description:
Returns a finished connection to the pool: puts it into the cache while it
holds fewer than `MAX_CONN_CTX_CACHE_CNT` entries, otherwise releases the
memory with `free_conn_ctx_mem()`. Bounds reuse while capping memory after
traffic spikes.

Return value: none.

## `static void init_new_conn_ctx(conn_t *conn, int conn_sock)` — internal (srv_conn_ctxt.c)

Parameters:
- `conn` — freshly allocated connection context
- `conn_sock` — descriptor of the accepted client socket

Description:
Prepares the coroutine of a new connection: `getcontext()`, stack bounds
from the context buffer, `uc_link` to the scheduler main context, entry
function `handle_one_connection()` via `makecontext()`. Stores the socket
descriptor and resets the state flags.

Return value: none.

# srv_conn_send_recv.h

Receiving and sending of raw data from/to the socket of the current
connection, implemented as non-blocking loops cooperating with the coroutine
scheduler. Implementation is in `srv_conn_send_recv.c` — the bridge between
the http layer (which operates on complete messages) and the asynchronous
socket I/O of the core.

## `ssize_t recv_from_curr_conn(char *buf, ssize_t to_recv, ssize_t n_recv, time_t timeout, bool terminator)`

Parameters:
- `buf` — buffer where received data are appended
- `to_recv` — maximal number of bytes to receive in this call
- `n_recv` — number of bytes already present in `buf` (append offset)
- `timeout` — limit for the whole operation in ns, TIMEOUT_INF for none
- `terminator` — stop receiving when "\r\n\r\n" appears in `buf`

Description:
Receives up to `to_recv` additional bytes from the client socket in a
non-blocking loop. Every `read()` returning EAGAIN/EWOULDBLOCK yields to the
scheduler (`swap_to_main_ctx()`) and retries; if the connection has been
idle too long (`curr_conn_active_timeout()`), it is marked inactive. A fatal
read error closes the connection and yields (never to return). Receiving
ends when: `to_recv` bytes are collected; the "\r\n\r\n" terminator is found
in `buf` (terminator mode, checked by `is_http_message_end()`); the
operation timeout is exceeded (checked after each successful read when
`timeout` != TIMEOUT_INF); or the client closed its side (read returns 0).

Return value: number of newly received bytes (>= 0), or -1 on fatal socket
error (unreachable in practice — the coroutine does not resume).

## `ssize_t send_to_curr_conn(const char *buf, ssize_t to_send, time_t timeout)`

Parameters:
- `buf` — data to send to the client
- `to_send` — number of bytes to send
- `timeout` — limit for the whole operation in ns, TIMEOUT_INF for none

Description:
Sending counterpart of `recv_from_curr_conn()`: sends `to_send` bytes in a
non-blocking loop. A `write()` returning EAGAIN/EWOULDBLOCK yields to the
scheduler and retries; prolonged idleness marks the connection inactive; a
fatal write error closes the connection and yields (never to return). The
loop ends when all bytes are sent, the operation timeout is exceeded (after
each successful write, if `timeout` != TIMEOUT_INF) or `write()` returns 0.

Return value: number of bytes sent (>= 0), or -1 on fatal socket error
(unreachable in practice — the coroutine does not resume).

## `static bool is_http_message_end(char *recv_buf, ssize_t prev_n_recv, ssize_t n_recv)` — internal (srv_conn_send_recv.c)

Parameters:
- `recv_buf` — receive buffer with message data collected so far
- `prev_n_recv` — number of bytes in the buffer before the last read
- `n_recv` — total number of bytes in the buffer after the last read

Description:
Checks whether the http header terminator "\r\n\r\n" is contained in the
received data. A buffer shorter than 4 bytes means no. A buffer ending with
the terminator is the common case of headers without body (GET/HEAD).
Otherwise the new chunk plus the 3-byte boundary before it are scanned, so a
terminator split between two reads is also detected. Used by
`recv_from_curr_conn()` with `terminator` = true.

Return value: true if the terminator sequence is found, false otherwise.

# srv_defs.h

Common definitions shared by all server modules. This header contains no
functions — only macros and constants:

- Size limits: `MAX_HTTP_BODY_SIZE` (max http body), `MAX_HTTP_HDR_SZIE`
  (max header section), coroutine context memory layout (`STACK_SIZE`,
  `RECV_BUF_SIZE`, `GUARD_SIZE`, `CTX_BUF_SIZE`), `SEND_PACKET_SIZE`
- Timeouts, in nanoseconds: `MAX_ACTIVE_TIMEOUT` (idle limit before a
  connection becomes inactive), `MAX_INACTIVE_TIMEOUT`, `CTX_SWAP_TIMEOUT`
  (max time in one coroutine before yielding), `CLIENT_OP_TIMEOUT` (limit
  for one receive/send operation), `TIMEOUT_INF` (no-timeout marker)
- Defaults and limits: `MAX_NUM_WORKERS`, `DEFAULT_SRV_PORT`,
  `DEFAULT_NUM_WORKERS`, `DEFAULT_BASE_DIR`, `DEFAULT_HOST_NAME`,
  `DEFAULT_DB_*` (MySQL defaults), `SRV_CONFIG_FILE`,
  `MIN_CONN_CTX_CACHE_CNT`/`MAX_CONN_CTX_CACHE_CNT`,
  `FS_ACCESS_BLOCK_SIZE` (file access chunk size)
- Log levels `LOG_ERROR`/`LOG_INFO1`/`LOG_INFO2` and the `LOG(lvl, ...)`
  macro: prints to `SRV_CONFIG.log_file_desc` with pid, severity label and
  calling function name, but only if `lvl` does not exceed the configured
  `log_level`; LOG_ERROR messages are flushed immediately; usable from both
  C and C++ sources

# srv_qlist.h

Minimal intrusive doubly-linked list in the style of the Linux kernel
`list_head`. Contains no functions — the container is fully implemented with
macros over the `struct qlist_head` node (`prev`/`next`), which is embedded
directly into the contained structure (e.g. `conn_t`), so no extra
allocations are needed.

Provided macros:
- `qlist_head_init(head)` — initialize an empty list head
- `qlist_empty(head)` — check whether the list is empty
- `qlist_entry(type, list_field, item_ptr)` — get the containing structure
  from a pointer to its embedded list node
- `qlist_first_entry(type, list_field, head)` — first entry of the list
- `qlist_next_entry(pos, list_field)` — entry following `pos`
- `qlist_entry_is_head(head, pos, list_field)` — whether `pos` is the head
- `qlist_add_head(head, item_ptr)` / `qlist_add_tail(head, item_ptr)` —
  insert an entry at the head/tail of the list
- `qlist_del_entry(item_ptr)` — unlink an entry from the list
- `qlist_foreach_entry(head, pos, list_field)` — iterate over all entries
- `qlist_foreach_entry_safe(head, pos, pos_n, list_field)` — iteration safe
  against removal of the current entry (used by the scheduler loop)
