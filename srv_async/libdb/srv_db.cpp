/* Interaction between server and mysql database */

#include <cerrno>
#include <memory>
#include <string>
#include <unistd.h>

#include <cppconn/connection.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>

#include "libdb/srv_db.h"
#include "srv_config.h"
#include "srv_defs.h"

std::unique_ptr<sql::Connection> db_conn;

extern "C" int open_srv_db(void)
{
    try {
        sql::Driver *driver = get_driver_instance();

        db_conn.reset(driver->connect(SRV_CONFIG.db_socket,
                SRV_CONFIG.db_user, SRV_CONFIG.db_password));

        std::unique_ptr<sql::Statement> stmt(db_conn->createStatement());

        /* Check that database db_name exists and is accessible */
        stmt->execute("USE " + std::string(SRV_CONFIG.db_name));

        /* Check read access */
        std::unique_ptr<sql::ResultSet>
                res(stmt->executeQuery("SHOW TABLES"));

        /* Check access for modification without touching existing
         * data: create, modify and drop temporary table */
        stmt->execute("CREATE TEMPORARY TABLE srv_db_check (id INT)");
        stmt->execute("INSERT INTO srv_db_check VALUES (1)");
        stmt->execute("DROP TEMPORARY TABLE srv_db_check");
    } catch (const sql::SQLException &e) {
        LOG(LOG_ERROR, "connection with database failed: %s "
                "(error %d, state %s)\n", e.what(), e.getErrorCode(),
                e.getSQLState().c_str());
        db_conn.reset();
        return -ECONNREFUSED;
    }

    LOG(LOG_INFO1, "connected to database %s\n", SRV_CONFIG.db_name);
    return 0;
}
