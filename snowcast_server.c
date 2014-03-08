// Macro for debug
#define DEBUG

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

#include "snowcast_global.h"
/* defines */
#define BACKLOG 10   // how many pending connections queue will hold
#define BUF_SIZE 256 // 256 bytes

#define MAX_CLIENT_NUM 10 // we can at most get 10 clients

#define HELLO_CMD 0
#define SETSTATION_CMD 1

#define WELCOME 0
#define ANNOUNCE 1
#define INVALID 2

/* global variables */
typedef struct Control
{
    uint8_t command_type;
    uint16_t info;
}Control_t;

typedef struct Welcome
{
    uint8_t reply_type;
    uint16_t num_stations;
}Welcome_t;

typedef struct Announce
{
    uint8_t reply_type;
    uint8_t songname_size;
    char* song_name;
}Announce_t;

typedef struct InvalidCommand
{
    uint8_t reply_type;
    uint8_t reply_string_size;
    char* reply_string;
}InvalidCommand_t;

typedef struct ServerInfo
{
    int cur_station;
    // to be added..
}ServerInfo_t;

typedef struct ClientInfo
{
    int state; // state, 0 for idle, 1 for active
    int socket;
    const char* ip_address;
    // to be added
}ClientInfo_t;

ServerInfo_t server;
ClientInfo_t clients[MAX_CLIENT_NUM];


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
	}
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 send_all: send the message in multiple passes.
 @return: -1 for failure, 0 for success.
 @param s: the socket.
 @param buf: the buffer.
 @param len: the length of the buffer sent.
 */
int send_all(int s, char *buf, int *len)
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

// TODO
void play_song()
{
    
}

// STREAM SONG
void stream_song()
{
    
}

/*
 init_server_local: initialize the local settings of the server
 */
void init_server_locl()
{
    // set the current station
    memset(&server, 0,sizeof(ServerInfo_t));
    server.cur_station = 1;
    
    // to be added
    
}

/*
 init_clients_info: initialize the clien info
 */
void init_clients_info()
{
    memset(clients, 0, sizeof(clients));
}

/*
 find_first_client: find the first idle client
 @return: return the index of first valid client, if full return -1
 */
int find_first_client()
{
    int i;
    for(i = 0; i < MAX_CLIENT_NUM;i++)
    {
        if(clients[i].state == 0)
            return i;
    }
    return -1;
}

/*
 find_client: find the client
 @param s: the socket number
 @return: the index if succeed, or -1 for not found
 */
int find_client(int s)
{
    int i;
    for(i = 0; i < MAX_CLIENT_NUM;i++)
    {
        if(clients[i].socket == s)
        {
            return i;
        }
    }
    return -1;
}

/*
 init_server_local: initialize the local settings of the server
 @return: no return value, always succeed
 */
void show_server_status()
{
    fprintf(stdout, "Server status:\n");
    fprintf(stdout, "current station: %d\n", server.cur_station);
}
/*
 init_listen: initialize the socket.
 @return: -1 for failure, 0 for success.
 @param tcp_port: the port for tcp.
 @param buf: the buffer.
 @param len: the length of the buffer sent.
 */
int init_listen(const char* tcp_port, const char* server_name)
{
    struct addrinfo hints, *ai, *p;
    int rv;
    int sockfd;
    
    memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(server_name, tcp_port, &hints, &ai)) != 0)
	{
		fprintf(stderr, "Snowcast_server: %s\n", gai_strerror(rv));
		return -1;
	}
    
	// search for the services
	for(p = ai; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd < 0) // connot listen
		{
			continue;
		}
        
        bool yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(bool)) == -1) {
            perror("setsockopt");
            exit(1);
        }
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0)
		{ // if failed we just close the socket
			close(sockfd);
			continue;
		}
        
		break;
	}
    // if we got here, it means we didn't get bound
	if (p == NULL)
	{
		fprintf(stderr, "Snowcast_server: failed to bind\n");
		return -1;
	}
	freeaddrinfo(ai); // all done with this
    return sockfd;
}

/*
 parse_and_send: parse the client and then send back feedback if necessary
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @parem buf: the buffer from user input
 */
int parse_and_send(int s, const char* buf)
{
#ifdef DEBUG
    fprintf(stdout, "Client data: %s", buf);
#endif
    // Do nothing if the buffer is empty
    if(buf == NULL)
        return 0;
    
    struct Control control;
    control = (*(struct Control*)buf);
    if(control.command_type == HELLO_CMD)
    {
#ifdef DEBUG
        fprintf(stdout, "I get hello from socket %d.\n", s);
#endif
        // send welcome
        /*const char* welcome = "welcome";
        int len = strlen("welcome");
        if((send_all(s, "welcome", &len)) < 0)
        {
            perror("send");
            return -1;
        }*/
        Welcome_t wel = {(uint8_t)(WELCOME), (uint16_t)(0)};
        int len = sizeof(Welcome_t);
        if((send_all(s, &wel, &len)))
        {
            perror("send");
            return -1;
        }
        
    }else if(control.command_type == SETSTATION_CMD)
    {
        // set station number
#ifdef DEBUG
        fprintf(stdout, "I get SetStation from socket %d.\n", s);
#endif
        
    }
    return 0;
}
/*
 server_listen: the main loop for server.
 @return: -1 for failure, 0 for success.
 @param sockfd: the socket.
 */
int server_listen(int sockfd)
{
    fprintf(stdout, "Snowcast_server: start listening\n");
    if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
        return -1;
    }
    //fprintf(stdout, "Snowcast_server: start listening\n");
    int i;
    int ret;
    int fdmax;
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;
    char buf[BUF_SIZE];
    char remoteIP[INET6_ADDRSTRLEN];
    
    FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds); // clear the read file descriptors
    
    // add the listener to the master set
	FD_SET(sockfd, &master);
    // keep track of the biggest file descriptor
	fdmax = sockfd; // so far, it's this one
    
	while(1) {
		read_fds = master; // copy it. the reason is that we need to reset the set once we do select.
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			return -1;
		}
		// run through the existing connections looking for data to read
		for(i = 0; i <= fdmax; i++)
		{
			if (FD_ISSET(i, &read_fds))
			{ // we got one!!
				if (i == sockfd)
				{
					// handle new connections
					addrlen = sizeof(remoteaddr);
					int newfd = accept(sockfd,
								   (struct sockaddr *)&remoteaddr,
								   &addrlen);
					if (newfd == -1)
					{
						perror("accept");
                        return -1;
					} else
					{
						FD_SET(newfd, &master); // add to master set
						if (newfd > fdmax)
						{    // keep track of the max
							fdmax = newfd;
						}
                        
                        /* Now we need to determine if we need
                           to add the new client to the server.
                           Clients may be full, in that case we 
                           send the newly connected socket a message
                           and then close it. Otherwise we push it into
                           the array
                         */
                        
                        int first = find_first_client();
                        if(first == -1)
                        {
                            const char* messg = "Server is busy, close the connection";
                            send(newfd, messg, strlen(messg),0);
                            close(newfd);
                        }else
                        {
                            /* We get a new client, push it into clientinfo */
                            clients[first].state = 1;
                            clients[first].socket = newfd;
                            clients[first].ip_address = inet_ntop(remoteaddr.ss_family,
                                                                get_in_addr((struct sockaddr*)&remoteaddr),
                                                                remoteIP, INET6_ADDRSTRLEN);
                            
                            fprintf(stdout,"snowcast_server: new connection from    %s on "
                                    "socket %d\n",
                                    clients[first].ip_address,clients[first].socket);
                        }
					}
				} else
				{
                    int nbytes;
					// else handle data from a client
					if ((nbytes = recv(i, buf, sizeof(struct Control), 0)) <= 0)
					{
                        int index = find_client(i);
						// got error or connection closed by client
                        
						if (nbytes == 0)
						{
							// connection closed
                            fprintf(stdout, "Snowcast_server: disconnected from %s \n", clients[index].ip_address);
                            fprintf(stdout,"Snowcast_server: socket %d hung up\n", i);
                        } else
						{
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                        memset(&clients[index], 0, sizeof(ClientInfo_t));
                    }
					else // send to parser
					{
						// print out the data from client
						/*fprintf(stdout, buf);
						int len = strlen("welcome");
						if((ret = send_all(i, "welcome", &len)) < 0)
						{
							perror("send");
						}*/
                        parse_and_send(i, buf);
					}
                }
            }
        }
    }
}

// usage
void usage()
{
	fprintf(stderr, "snowcast_server [tcpport] [file1] [file2] ...\n");
}

int main(int argc, char* argv[])
{
    const char** file_name;
    const char* tcp_port;
	if(argc < 2)
	{
		usage();
		exit(1);
	}
	else
	{
		tcp_port = argv[1];
		file_name = (char**)malloc(sizeof(char*)*(argc-2));
        if(file_name == NULL)
        {
            fprintf(stderr, "No spaces for heap.\n");
            exit(1);
        }
        for(int i = 2; i < argc; i++)
		{
			file_name[i-2] = argv[i];
		}
	}

    int socket;
    
    if((socket = init_listen(tcp_port, NULL)) < 0)
    {
        fprintf(stderr, "error binding socket.\n");
        exit(1);
    }
    
    init_server_locl();
    init_clients_info();
    
    if(server_listen(socket) == -1)
    {
        fprintf(stderr, "error in server.");
        exit(1);
    }
    close(socket);
	return 0;
}