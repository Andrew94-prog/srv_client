## Asynchronous coroutine-based http server

### 1. Key properties
- Server supports GET/HEAD/POST/PUT/DELETE/PATCH methods
- Server works directly with clients, no proxy mode

### 2. Brief description of sources

- `srv_async.c`, `srv_config.c`, `srv_routines.c`, `srv_sock.c` — main part of server, includes
  creating and control of server workers, accepting and handling new connections
- `srv_conn_ctxt.c` — coroutine context management, includes creating
  and deleting coroutines with context for each connection, scheduling of coroutines
- `srv_conn_send_recv.c` — functions for receiving/sending raw data from/to socket
- `http/srv_http_send_recv.cpp`, `http/srv_http_msg.cpp` — receiving and sending of http
  messages from/to clients, base class of http messages
- `http/srv_http_request_msg.cpp`, `http/srv_http_response_msg.cpp` — common classes
  of http requests and responses
- `http/srv_http_request_<type>.cpp` — classes of http requests for each supported method

### 3. General rules

- Do not edit core part of server, which is implemented in `*.c` files. If you consider,
  that it is necessary to edit this part of server, propose adding of a new function, not
  editing an already existing one. You _must_ ask before doing it.
- Use C++ code style in `*.cpp` files for handling of http messages. Exceptions:
    - Calling function from *.c source in *.cpp source.
    - Use LOG macros for printing debug messages

### 4. Standard requirements

- C++20 standard for `*.cpp` files
- C11 standard for `*.c` files
