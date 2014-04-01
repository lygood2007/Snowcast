/*
 Author: yanli, xiaoyi
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

#define NUM_RECEIVER 2 // Two thread to receive the stream from server
#define BUF_SIZE 2000

pthread_t stream_receiver[NUM_RECEIVER];

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
/*
 receive: receive loop
 @return: nothing to return, always NULL
 @param sockets: the argument list
 */
void receive(int sockets)
{
    int sockfd = sockets;
    char buf[BUF_SIZE];
    int nbytes;
    struct sockaddr their_addr;
    socklen_t addr_len;
    addr_len = sizeof their_addr;
    char ip_addr[INET6_ADDRSTRLEN];
    while(1)
    {
        if ((nbytes = recvfrom(sockfd, buf, BUF_SIZE , 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        /*fprintf(stderr, "Listener: got packet from %s\n",
               inet_ntop(their_addr.ss_family,
                         get_in_addr((struct sockaddr *)&their_addr),
                         ip_addr, sizeof ip_addr));
        fprintf(stderr, "Listener: packet is %d bytes long\n",nbytes);
        buf[nbytes] = '\0';
        fprintf(stderr, "Listener: packet contains \"%s\"\n", buf);*/
        // output to stdout
        if(write(STDOUT_FILENO, buf, nbytes) != nbytes)
        {
            perror("write");
            exit(1);
        }
    }
//    return NULL;
}

/*
 open_connection: bind a UDP socket to a port.
 @return: socket number or -1 for error.
 @param udp_port: the udp portnumber.
 */
int open_connection(const char* udp_port)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    //hints.ai_flags = ; // use my IP
    if ((rv = getaddrinfo(NULL, udp_port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "Getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "Listener: failed to bind socket\n");
        return -1;
    }
    freeaddrinfo(servinfo);
    
    fprintf(stderr, "Listener: waiting to recv stream...\n");
    return sockfd;
}
//usage
void usage()
{
    fprintf(stderr, "snowcast_client port\n");
}

int main(int argc, char* argv[])
{
    const char* udp_port;
    //int i;
    int sockfd;
    if(argc != 2)
    {
        usage();
        exit(1);
    }
    else
    {
        udp_port = argv[1];
    }
    if((sockfd = open_connection(udp_port)) == -1)
    {
        fprintf(stderr, "Error in binding the socket.\n");
        exit(1);
    }
    
    receive(sockfd);

    /*for(i = 0; i < NUM_RECEIVER; i++)
    {
        pthread_create(&stream_receiver[i], NULL, receive, (void*)((long)sockfd));
    }
    
    for(i = 0; i < NUM_RECEIVER; i++)
    {
        int tmp;// no use for the return value
        pthread_join(stream_receiver[i],&tmp);
    }*/
    // close the socket
    close(sockfd);
	return 0;
}
