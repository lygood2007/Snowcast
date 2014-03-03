// Macro for debug
#define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <pthread.h>

/* defines */
#define HELLO_CMD 0
#define SETSTATION_CMD 1
#define BUF_SIZE 256 // 256 bytes

//#define ERROR

/* global structure declaration */
struct Hello
{
    uint8_t command_type;
    uint16_t udp_port;
};

struct SetStation
{
    uint8_t command_type;
    uint16_t station_number;
};

/* global variables for thread id */
pthread_t sender;
pthread_attr_t send_attr;

pthread_t receiver;
pthread_attr_t receiver_attr;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
	}
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 send_message: the thread for handling user input and send message
 @return: nothing to return, always NULL
 @param sockets: the argument list
 */
void* send_message(void* sockets)
{
    char buf[BUF_SIZE];
    int sockfd = (int)(sockets);
    while(1)
    {
        // get user input
        fgets(buf, BUF_SIZE, stdin);
#ifdef DEBUG
        fprintf(stdout, "user input:%s\n", buf);
#endif
        if(send(sockfd, buf, strlen(buf),0) < 0)
        {
            perror("send");
            break;
        }
    }
    
    return NULL;
}

/*
 recv_message: the thread for handling received message
 @return: nothing to return, always NULL
 @param sockets: the argument list
 */
void* recv_message(void* sockets)
{
    int sockfd = (int)sockets;
    int numbytes;
    char buf[BUF_SIZE];
    while(1)
    {
        if ((numbytes = recv(sockfd, buf, BUF_SIZE-1, 0)) <= 0) {
			// Error when receive
            if(numbytes == 0)
            {
                fprintf(stdout, "closing connection.\n");
            }else
            {
                perror("recv");
            }
            exit(1);
		}
        buf[numbytes] = '\0';
#ifdef DEBUG
        snprintf(buf, BUF_SIZE-1, "receive:%s\n",buf);
        fprintf(stdout, buf);
#endif
    }
    return NULL;
}

/*
 open_connection: open the connection to music server.
 @return: socket number or -1 for error.
 @param tcp_port: the tcp port number.
 @param udp_port: the udp portnumber.
 @param server_name: the server's IP address or name.
 */
int open_connection(const char* tcp_port, const char* udp_port, const char* server_name)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    memset(s, 0, sizeof(s));
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((rv = getaddrinfo(server_name, tcp_port, &hints, &servinfo)) != 0)
	{
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
	}
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
	{
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
							 p->ai_protocol)) == -1)
		{
            perror("client: socket");
			continue; }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
            close(sockfd);
            perror("client: connect");
            continue;
		}
		break;
	}
    if (p == NULL)
	{
        fprintf(stderr, "client: failed to connect\n");
        return -1;
	}
	freeaddrinfo(servinfo); // all done with this structure
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			  s, sizeof s);
    fprintf(stdout,"client: connecting to %s\n", s);
    
    return sockfd;
}

// usage
void usage()
{
	fprintf(stderr, "snowcast_control server_name port udpport\n");
}

int main(int argc, char *argv[])
{
    const char* tcp_port = NULL;
    const char* udp_port = NULL;
    const char* server_name = NULL;
    void* ret;
	if (argc != 4) {
        usage();
        exit(1);
	}else
	{
		server_name = argv[1];
		tcp_port = argv[2];
		udp_port = argv[3];
	}
    
    int sockfd = open_connection(tcp_port, udp_port, server_name);
    if(sockfd == -1)
    {
        exit(1);
    }
    
    // initialize the attribute
    pthread_attr_init(&send_attr);
    pthread_attr_init(&receiver_attr);
    
    // 1MB stack size
    pthread_attr_setstacksize(&send_attr, 1024*1024);
    pthread_attr_setstacksize(&receiver_attr, 1024*1024);
    
    pthread_create(&sender, &send_attr, send_message,(void*)sockfd);
    pthread_create(&receiver, &receiver_attr, recv_message, (void*)sockfd);
    
    pthread_join(sender, &ret);
    pthread_join(receiver, &ret);
    
    pthread_attr_destroy(&receiver_attr);
    pthread_attr_destroy(&send_attr);
    
    close(sockfd);
	return 0; 
}
