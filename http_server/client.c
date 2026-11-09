#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <netinet/tcp.h>
#define COMM_IMPLEMENTATION
#include "common.h"
//#define REMOTE


void *receive_thread(void *arg);

int server_fd;
int is_vibrating = 0;

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
   
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(int argc, char **argv) {
  
#ifdef REMOTE
    if (argc != 3) {
        fprintf(stderr, "Usage: %s SERVER_IP\n", argv[0]);
        return 1;
    }   
#endif //remote 

    struct addrinfo hints = {0};
    struct addrinfo *res; //a linked lsit

    hints.ai_family = AF_UNSPEC;    //dont care IPv4 or 6
    hints.ai_socktype = SOCK_STREAM;
#ifndef REMOTE
    hints.ai_flags = AI_PASSIVE;    //host's ip
#endif 

#ifdef REMOTE
    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
#else
    if (getaddrinfo(NULL, "8080", &hints, &res) != 0) {
#endif //remote
        printf("getaddrinfo fails\n");
        exit(0);
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        printf("socket fails.\n");
        exit(0);
    }

    if (connect(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        printf("connect fails\n");
        exit(0);
    }
    
    int flag = 1;
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY,
           &flag, sizeof(flag));

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_thread, NULL );

    enableRawMode();

    uint8_t recv_data[MAX_DATA_SIZE - 1];
    
    while (1) {
	    if (is_vibrating) {
	        is_vibrating = 0;
	        printf("hit!!!!\n");
	    }
        uint8_t data[MAX_DATA_SIZE - 1] = {0};
	    char c;
            if(read(STDIN_FILENO, &c, 1 )) {
            
            //printf("character \'%c\' is read\n", c);
            data[0] = (uint8_t)c;
            Send(data, URL_CONTROLLER_INPUT, server_fd);
	    }
	    struct timespec ts = {
        	    .tv_sec = 0,
        	    .tv_nsec = 16000000L
            };
	    nanosleep(&ts, NULL);
	    tcflush(STDIN_FILENO, TCIFLUSH);
    }

   
    
    
    freeaddrinfo(res);
    return 69;
}

void *receive_thread(void *arg) {
    while (1) {
	uint8_t data[MAX_DATA_SIZE - 1];
        uint8_t url;
        Recv(data, &url, server_fd);
        is_vibrating = 1;
    }
}

