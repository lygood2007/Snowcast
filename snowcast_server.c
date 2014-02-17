#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <netdb.h>
#include <signal.h>

/* defines */
#define BACKLOG 10   // how many pending connections queue will hold
#define MAX_SONGS 10 // max songs

/* global variables */
uint8_t reply_type;
uint16_t num_stations;
uint8_t songname_size;
uint8_t replystring_size;

const char* songname = NULL;
const char* replystring = NULL;
const char* tcpport = NULL; // port
const char* server_name = NULL; // server name, ip
const char* files[MAX_SONGS] = {NULL};

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
	fprintf(stdout, "snowcast_server tcpport file1 file2 ...\n");
}

// send message
int sendall(int s, char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;
    while(total < *len) 
	{
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) 
		{
			break;
		}
        total += n;
        bytesleft -= n;
	}
    *len = total; // return number actually sent here
    return n == -1?-1:0; // return -1 on failure, 0 on success
}

int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		usage();
		exit(1);
	}
	else
	{
		tcpport = argv[1];
		for(int i = 2; i < argc&& i < MAX_SONGS; i++)
		{
			files[i-2] = argv[i];
		}
	}
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
	int fdmax;
	int listener;
	int newfd;
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;
	char buf[256];    // buffer for client data
	int nbytes;
	int ret;
	char remoteIP[INET6_ADDRSTRLEN];

	int i, rv;
	struct addrinfo hints, *ai, *p;
	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds); // clear the read file descriptors

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(NULL, tcpport, &hints, &ai)) != 0) 
	{
		fprintf(stderr, "snowcast_server: %s\n", gai_strerror(rv));
		exit(1);
	}

	// search for the services
	for(p = ai; p != NULL; p = p->ai_next) 
	{
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) // connot listen
		{ 
			continue;
		}

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
		{ // if failed we just close the socket
			close(listener);
			continue; 
		}
		break;
	}
    // if we got here, it means we didn't get bound
	if (p == NULL)
	{
		fprintf(stderr, "snowcast_server: failed to bind\n");
		exit(1);
	}
	freeaddrinfo(ai); // all done with this
    // listen
	if (listen(listener, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
    // add the listener to the master set
	FD_SET(listener, &master);
    // keep track of the biggest file descriptor
	fdmax = listener; // so far, it's this one

	while(1) {
		read_fds = master; // copy it. the reason is that we need to reset the set once we do select.
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			exit(1);
		}
		// run through the existing connections looking for data to read
		for(i = 0; i <= fdmax; i++) 
		{
			if (FD_ISSET(i, &read_fds)) 
			{ // we got one!!
				if (i == listener)
				{
					// handle new connections
					addrlen = sizeof(remoteaddr);
					newfd = accept(listener,
								   (struct sockaddr *)&remoteaddr,
								   &addrlen);
					if (newfd == -1) 
					{
						perror("accept");
					} else
					{
						FD_SET(newfd, &master); // add to master set
						if (newfd > fdmax) 
						{    // keep track of the max
							fdmax = newfd;
						}
						fprintf(stdout,"snowcast: new connection from %s on "
							   "socket %d\n",
							   inet_ntop(remoteaddr.ss_family,
										 get_in_addr((struct sockaddr*)&remoteaddr),
										 remoteIP, INET6_ADDRSTRLEN),newfd); 
					}
				} else 
				{
					// else handle data from a client
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) 
					{
						// got error or connection closed by client
						if (nbytes == 0) 
						{
							// connection closed
                            fprintf(stdout,"snowcast_server: socket %d hung up\n", i);
                        } else 
						{
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    }
					else // send to parser
					{
						// print out the data from client
						fprintf(stderr, buf);
						int len = strlen("welcome");
						if((ret = sendall(i, "welcome", &len)) < 0)
						{
							perror("send");
						}
					}
                }
            }
        }
    }
	return 0;
}
