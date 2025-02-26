#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

//#define DBG

#ifdef DBG
#define pr_debug(str) printf(str)
#else
#define pr_debug
#endif

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    int sock = 0, srv_port = 8080, opt = 1;
    char srv_addr[20] = "127.0.0.1";
    struct sockaddr_in serv_addr;
    char message[BUFFER_SIZE] = "Hello from client";
    char buffer[BUFFER_SIZE] = {0};

    if (argc >= 3) {
        srv_port = atoi(argv[2]);
        strncpy(srv_addr, argv[1], 20);
    }

again:

    /* Create socket file descriptor */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client: socket creation error");
        return -1;
    }

    /* Forcefully attaching socket to the port */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Client: setsockopt failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(srv_port);

    /* Convert IPv4 and IPv6 addresses from text to binary form */
    if (inet_pton(AF_INET, srv_addr, &serv_addr.sin_addr) <= 0) {
        perror("Client: invalid address Address not supported");
        return -1;
    }

    /* Connect to the server */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        goto again;
    }

    /* Send a message to the server */
    send(sock, message, sizeof(message), 0);
    pr_debug("Client: message sent: %s\n", message);

    /* Read the response from the server */
    read(sock, buffer, BUFFER_SIZE);
    pr_debug("Client: response from server: %s\n", buffer);

    /* Close the socket */
    close(sock);

    goto again;

    return 0;
}