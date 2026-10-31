#ifndef SRV_CONFIG_H
#define SRV_CONFIG_H

#define MAX_LINE_LEN 1024

/* Help message with expected format of config file lines,
 * printed if config file could not be parsed */
#define HELP_MSG \
    "Expected format of config file lines:\n" \
    "port: PORT_NUM (1-65535)\n" \
    "num_workers: NUM_WORKERS (1-%d)\n" \
    "log_file: path_to_log_file\n" \
    "log_level: LOG_LEVEL (0-%d)\n" \
    "base_dir: path_to_server_base_directory\n" \
    "host_name: host_name_of_server\n" \
    "db_socket: path_to_mysql_socket_file\n" \
    "db_name: name_of_mysql_database\n" \
    "db_user: user_name_to_access_database\n" \
    "db_password: user_password_to_access_database\n"

typedef struct srv_config {
    int port;
    int num_workers;
    char *log_file_name;
    FILE *log_file_desc;
    int log_level;
    char *base_dir;         /* server's base directory */
    char *host_name;        /* host name of server, used in
                                absolute urls and redirects */
    char *db_socket;        /* path to socket file for access to mysql */
    char *db_name;          /* name of mysql database */
    char *db_user;          /* user name to access database */
    char *db_password;      /* user's password to access database */
} srv_config_t;

extern srv_config_t SRV_CONFIG;

int parse_srv_config(void);

/* Check that base_dir path is accessible for read and write.
 * Returns 0 on success, negative errno code on failure */
int validate_srv_base_dir(const char *path);

#endif /* SRV_CONFIG_H */
