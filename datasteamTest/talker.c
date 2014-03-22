/*
 ** talker.c -- a datagram "client" demo
 */

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

#define SERVERPORT "4950"	// the port users will be connecting to

int main(int argc, char *argv[])
{
    int i = 0;
    while(1){
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
    FILE *ptr_file;
    char buf[1000];
    
	if (argc != 3) {
		fprintf(stderr,"usage: talker hostname message\n");
		exit(1);
	}
    
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
    
	if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
    
	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
			perror("talker: socket");
			continue;
		}
        
		break;
	}
    
	if (p == NULL) {
		fprintf(stderr, "talker: failed to bind socket\n");
		return 2;
	}
    
    // read data
    ptr_file = fopen(argv[2],"r"); // read mode
    
    if(!ptr_file)
    {
        perror("Error while opening the file.\n");
        exit(EXIT_FAILURE);
    }
    
    while(fgets(buf,1000, ptr_file)!= NULL ) {
        printf("%s\n",buf);
    }
    
	if ((numbytes = sendto(sockfd, buf, strlen(buf), 0,
                           p->ai_addr, p->ai_addrlen)) == -1) {
		perror("talker: sendto");
		exit(1);
	}
    
	freeaddrinfo(servinfo);
    
	printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
	close(sockfd);
        
    // loop counter
    i++;
    printf("\n%d\n", i);
    }
    
	return 0;
}