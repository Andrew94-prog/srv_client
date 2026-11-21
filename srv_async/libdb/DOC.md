# Documentation of database library (directory `libdb/`)

Interface between the server and the MySQL database: opening of the
connection at worker startup and the global connection object used by future
api handlers. Implementation is in `srv_db.cpp` and uses MySQL
Connector/C++.

# libdb/srv_db.h

Interface between the server and the mysql database. The function
`open_srv_db()` has C linkage and is callable from both C and C++ sources
(it is called from the C core in `srv_routines.c`); the connection object
`db_conn` is usable only from C++ sources.

Declared global variable:
- `db_conn` — `std::unique_ptr<sql::Connection>`, the connection with the
  mysql database; valid only after a successful call of `open_srv_db()`,
  reset to null on failure

## `int open_srv_db(void)`

Parameters:
- none

Description:
Establishes the connection between a worker process and the mysql database
using `SRV_CONFIG` settings: `db_socket` as the endpoint, `db_user` /
`db_password` as credentials; the connection is stored in the global
`db_conn`. Then verifies that the database `db_name` is actually usable:
"USE <db_name>" (exists), "SHOW TABLES" (read access) and a create/insert/
drop cycle on a temporary table (modification access) — without touching
existing data. An sql::SQLException is caught here: it is logged with the
mysql error code and SQLState, `db_conn` is reset and an error is returned
(the caller terminates the worker). Called once at the start of
`handle_connections_routine()` in every worker.

Return value: 0 on success, -ECONNREFUSED if the connection or any of the
access checks failed.
