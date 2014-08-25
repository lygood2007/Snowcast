/*
 Source file: snowcast_listener.c
 Brief: Implemented the functionality of snowcast listner.
        Basically, it opens the udp port and continuously listen to the server
 Author: yanli (yan_li@brown.edu)
 Time stamp: 03/31/2014
 */

#define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

#define NUM_RECEIVER 2 /* two thread to receive the stream from server */
#define BUF_SIZE 2000 /* buffer size */

pthread_t stream_receiver[NUM_RECEIVER]; /* global variable: receivers*/

/* forward declaration */
void receive(int sockets);
int open_connection(const char* udp_port);
void usage();

/*
 receive: receive loop
 @param sockets: the socket id
 */
void receive(int sockets)
{
    int sockfd = sockets;
    char buf[BUF_SIZE] = {0};
    int nbytes = 0;
    struct sockaddr their_addr;
    socklen_t addr_len = 0;
    addr_len = sizeof(their_addr);
    
    /* trace last 4 bytes */
    char last_four_bytes[5] = {0};
    while (1)
    {
        if ((nbytes = recvfrom(sockfd, buf, BUF_SIZE , 0,
                              (struct sockaddr *)&their_addr, &addr_len)) == -1)
         {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        if (nbytes >= 4)
        {
            strncpy(last_four_bytes, buf, 4);
            last_four_bytes[4] = '\0';
            if (strcmp(last_four_bytes, "STOP") == 0)
            {
                exit(0);
            }
        }
        
        /* write to the output */
        if (write(STDOUT_FILENO, buf, nbytes) != nbytes)
        {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }
}

/*
 open_connection: bind a UDP socket to a port.
 @return: socket number or -1 for error.
 @param *udp_port: the pointer to the udp portnumber.
 */
int open_connection(const char* udp_port)
{
    int sockfd = 0;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    int rv = 0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(NULL, udp_port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "Getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    /* loop through all the results and bind to the first we can */
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) 
        {
            perror("socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
        {
            close(sockfd);
            perror("bind");
            continue;
        }
        break;
    }
    if (p == NULL) 
    {
        fprintf(stderr, "Listener: failed to bind socket\n");
        return -1;
    }
    freeaddrinfo(servinfo);
    
    fprintf(stderr, "Listener: waiting to recv stream...\n");
    return sockfd;
}

/*
 usage: print the usage of this program
 */
void usage()
{
    fprintf(stderr, "snowcast_client [port]\n");
}

int main(int argc, char* argv[])
{
    const char* udp_port = NULL;
    int sockfd = 0;

    if (argc != 2)
    {
        usage();
        exit(EXIT_FAILURE);
    }
    else
        udp_port = argv[1];

    if ((sockfd = open_connection(udp_port)) == -1)
    {
        fprintf(stderr, "Error in binding the socket.\n");
        exit(EXIT_FAILURE);
    }
    
    /* infinite loop to receive data stream */
    receive(sockfd);
    
    /* close the socket */
    close(sockfd);
	return 0;
}
