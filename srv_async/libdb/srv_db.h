#ifndef SRV_DB_H
#define SRV_DB_H

/* Interface between server and mysql database.
 * open_srv_db() is callable from both C and C++ sources,
 * db_conn only from C++ sources */

#ifdef __cplusplus
extern "C" {
#endif

/* Establish connection with mysql database using 'db_socket',
 * 'db_user', 'db_password' from SRV_CONFIG and check that
 * database 'db_name' is accessible for read and modification.
 * On success connection is stored in db_conn.
 * Returns 0 on success, negative error code on failure */
int open_srv_db(void);

#ifdef __cplusplus
}

#include <memory>

namespace sql {
class Connection;
}

/* Connection with mysql database, valid after successful
 * call of open_srv_db() */
extern std::unique_ptr<sql::Connection> db_conn;

#endif /* __cplusplus */

#endif /* SRV_DB_H */
