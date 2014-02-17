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

/* defines */

#define MAX_DATA_SIZE 100 // max number of bytes we can get at once
#define HELLO_CMD 0
#define SETSTATION_CMD 1
#define SEND_BUF_SIZE 512

/* global variabls */
uint8_t command_type;
uint16_t udp_port;
uint16_t station_number;

const char* port = NULL;
const char* udpport = NULL;
const char* server_name = NULL;
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
	}
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// usage
void usage()
{
	fprintf(stderr, "snowcast_control server_name port udpport\n");
}

int main(int argc, char *argv[])
{
	if (argc != 4) {
        usage();
        exit(1);
	}else
	{
		server_name = argv[1];
		port = argv[2];
		udpport = argv[3];
	}
    int sockfd, numbytes;
	char send_buf[MAX_DATA_SIZE];
    char recv_buf[MAX_DATA_SIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((rv = getaddrinfo(server_name, port, &hints, &servinfo)) != 0) 
	{
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
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
        exit(1);
	}
	freeaddrinfo(servinfo); // all done with this structure
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			  s, sizeof s);
    fprintf(stdout,"client: connecting to %s\n", s);
    
	//send hello to the server
    sprintf(send_buf, "%d", HELLO_CMD);
	if(send(sockfd, send_buf,strlen(send_buf),0) < 0)
	{
		perror("send");
		exit(1);
	}
	while(1)
	{
		if ((numbytes = recv(sockfd, recv_buf, MAX_DATA_SIZE-1, 0)) == -1) {
			perror("recv");
		}
		else if(numbytes == 0)
		{
			fprintf(stdout, "closing connection.\n");
			break;
		}
		recv_buf[numbytes] = '\0';
		fprintf(stderr,"client: received '%s'\n",recv_buf);
	}
    close(sockfd);
	return 0; 
}
