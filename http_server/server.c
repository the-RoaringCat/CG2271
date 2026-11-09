/*
    I learn the setup from here
    https://beej.us/guide

*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/tcp.h>
#define COMM_IMPLEMENTATION
#include "common.h"


int main(int argc, char **argv) {

     if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    struct addrinfo hints = {0};
    struct addrinfo *res; //a linked lsit

    hints.ai_family = AF_INET; //AF_UNSPEC;    //dont care IPv4 or 6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    //host's ip

    if (getaddrinfo(NULL, argv[1], &hints, &res) != 0) {
        printf("getaddrinfo fails\n");
        exit(0);
    }

    int socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        printf("socket fails.\n");
        exit(0);
    }

    if (bind(socket_fd, res->ai_addr, res->ai_addrlen) == -1) {
        printf("bind fails\n");
        exit(0);
    }
    
    if (listen(socket_fd, 20) == -1) {
        printf("listen fails\n");
        exit(0);
    }
    
    int flag = 1;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY,
           &flag, sizeof(flag));
    
    struct pollfd fds[2];
    
    for (int i = 0; i < 2; i++) {
        int client_fd = accept(socket_fd, NULL, NULL);
        fds[i].fd = client_fd;
        fds[i].events = POLLIN; //only interested when there is input to be read

        if (client_fd == -1) {
            perror("accept");
            exit(1);
        }

        printf("Client %d connected\n", i);
    }

    uint8_t controller_input[MAX_DATA_SIZE - 1] = {0};
    uint8_t controller_output[MAX_DATA_SIZE - 1] = {0};
    uint8_t game_input[MAX_DATA_SIZE - 1] = {0};
    uint8_t game_output[MAX_DATA_SIZE - 1] = {0};
    
       


    while (1) {
        if (poll(fds, 2, -1) == -1) { //poll for two with infinite timeout
            perror("poll");
            exit(1);
        } 

        for (int i = 0; i < 2; i++) {
            if ((fds[i].revents & POLLIN) != POLLIN) continue;

            // there is POLLIN
            // receive the post and push to the client
            uint8_t data[MAX_DATA_SIZE - 1] = {0};
            uint8_t status_url = 0;
            Recv(data, &status_url, fds[i].fd);
            Send(data, status_url, fds[1 - i].fd);    
        }
    }

    
    freeaddrinfo(res);
    return 69;
}

