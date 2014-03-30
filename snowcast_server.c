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
#include <assert.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "snowcast_global.h"
/* defines */
#define BACKLOG 10   // how many pending connections queue will hold
#define BUF_SIZE 1024 // 256 bytes

#define MAX_CLIENT_NUM 10 // we can at most get 10 clients

#define HELLO_CMD 0
#define SETSTATION_CMD 1

#define WELCOME 0
#define ANNOUNCE 1
#define INVALID 2

#define MAX_STATION_NUM 30
#define SONG_GROUPS 4

typedef struct Control
{
    uint8_t cmd;
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
    const char* song_name;
}Announce_t;

typedef struct InvalidCommand
{
    uint8_t reply_type;
    uint8_t reply_string_size;
    const char* reply_string;
}InvalidCommand_t;

typedef struct Station
{
    const char* cur_song;
    int fd;
}Station_t;

typedef struct ServerInfo
{
    int cur_station;
    // to be added..
}ServerInfo_t;

typedef enum State
{
    NO_STATE,
    INIT_STATE,
    HANDSHAKED
}State_t;

typedef struct ClientInfo
{
    State_t state;
    int socket; // for tcp
    const char* ip_address; // for tcp
    uint16_t udp_port;
    int udp_sock; // for udp
    struct addrinfo* udp_addrinfo; // for udp
    // to be added
    
}ClientInfo_t;

ServerInfo_t server;
ClientInfo_t clients[MAX_CLIENT_NUM];
Station_t stations[1]; // TODO: for testing now
pthread_t song_threads[1]; // TODO: for testing now, should be array later
const char* song_files[MAX_CLIENT_NUM];

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
    fprintf(stderr, "bytes:%d\n",*len);
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

/*
 read_line: read a total line from socket
 @return: the length in bytes that have been read, or 0 for disconnection and -1 for error
 @param fd: the file descriptor
 @param data: the buffer
 */
int read_line(int fd, char data[], int maxlen)
{
    int len = 0;
    while (len < maxlen)
    {
        char c;
        int ret = recv(fd, &c, 1,0);
        if(ret == 0)
        {
            return 0;
        }
        else if (ret < 0)
        {
             break; // error
        }
        if (c == '\n')
        {
            data[len] = 0;
            return len; // EOF reached
        }
        data[len++] = c;
    }
    return -1;
}

/*
 build_udpconnection: initialize the socket for udp connection.
 @param udp_port: the udp port
 @param index: index in the client array
 @return: 0 for success and -1 for failure
 */
int build_udpconnection(const char* udp_port, int index)
{
    //if(clients[index].ip_address == NULL)
      //  return 0;
    assert(clients[index].ip_address != NULL);
    assert(clients[index].socket != 0);
    assert(clients[index].state != NO_STATE);
    int sockfd;
    int rv;
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    clients[0].ip_address = "127.0.0.1";
    const char* ip_addr = clients[index].ip_address;
    if ((rv = getaddrinfo(ip_addr, udp_port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "snowcast_server: getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             IPPROTO_UDP)) == -1) {
            perror("socket");
            continue;
        }
        
        break;
    }
    
    if (p == NULL) {
        fprintf(stderr, "snowcast_server: failed to bind socket\n");
        return -1;
    }
    
    clients[index].udp_addrinfo = p;
    clients[index].udp_sock = sockfd;
    return 0;
}

/*
 loop_song: loop the song and send the udp package to clients if there are.
 @param fd: the file descriptor of the song
 */
int loop_song(int fd)
{
    assert(fd > 0);
    /* one byte one time*/
    struct timeval tm;
	int time1, time2;
    int num_bytes = 0;
    char buf[BUF_SIZE];
    char* buf_ptr = buf;
    while(1)
    {
        int loop = 0;
        gettimeofday(&tm,NULL);
		time1 = tm.tv_sec*1000000 + tm.tv_usec;

        while(loop != 16)
        {
            memset(buf,0,BUF_SIZE);
            int ret = read(fd, buf_ptr, BUF_SIZE);
            if(ret < 0)
            {
                perror("read");
                exit(EXIT_FAILURE);
            }
            else if(ret == 0)
            {
                // end of the file, seek to the beginning
                lseek(fd, 0, SEEK_SET);
                loop = 16;
                printf("restart");
                continue;
            }
            for(int i = 0; i < 1; i++)
            {
                if(clients[i].state == HANDSHAKED && clients[i].udp_sock != 0)
                {
                    
                    if ((num_bytes = sendto(clients[i].udp_sock, buf_ptr, ret, 0,
                                            clients[i].udp_addrinfo->ai_addr, clients[i].udp_addrinfo->ai_addrlen)) == -1) {
                        perror("sendto");
                    }
                }
            }
            loop++;
            //usleep(30);
        }
        loop  = 0;
		gettimeofday(&tm, NULL);
		time2 = tm.tv_sec*1000000 + tm.tv_usec ;
		int time_elapsed = time2 - time1;
		if(time_elapsed <= 1000000){
			//printf("Sleep\n");
			usleep(1000000 - time_elapsed);
		}
    }
}

/*
 loop_song: loop the song and send the udp package to clients if there are.
 @param fd: the file descriptor of the song
 */
void* loop_song_thread(void* args)
{
    //int index = (int)args;
    //int start = STATION_NUM/SONG_GROUPS;
    
    // TODO: currently responsible for only one station
    // TODO: use event-driven select
    if(stations[0].fd != 0)
    loop_song(stations[0].fd);
    
    return NULL;
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
 init_songs: basically open the files of those songs
 @param files: file names of mp3
 @param len: the lenth of the files array
 */
void init_songs(const char** files, int len)
{
    assert(len >= 0);
    assert(len < MAX_STATION_NUM);
    
    if(files == NULL)
    {
        fprintf(stderr, "snowcast_server: init_songs failed.\n");
        exit(EXIT_FAILURE);
    }
    for(int i = 0; i < len; i++)
    {
        // open the files
        int fd = 0;
        if((fd = open(files[i], O_RDONLY, 0))<0)
        {
            perror("open");
            // if it fails here, skip over this iteration
        }else
        {
            stations[i].fd = fd;
            stations[i].cur_song = files[i];
        }
    }
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
        if(clients[i].state == NO_STATE)
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
        freeaddrinfo(ai);
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
            freeaddrinfo(ai);
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
        freeaddrinfo(ai);
		return -1;
	}
	freeaddrinfo(ai); // all done with this
    return sockfd;
}

/*
 init_stations: initialize the song threads
 */
void init_stations()
{
    /*stations[0].cur_song = "LALALA";
    */
    /* init threads */
    pthread_create(&song_threads[0], NULL, loop_song_thread, (void*)0);
}

/*
 release_stations: wait for song threads to end
 */
void release_stations()
{
    int ret;
    pthread_join(song_threads[0], (void*)(&ret));
}
/*
 close_client: close the socket and ret the structure
 @param i: the index
 @param master: the master set
 */
void close_client(int i, fd_set master)
{
    assert(i>=0 && i < MAX_CLIENT_NUM);
    assert(clients[i].socket != 0);
    close(clients[i].socket); // bye!
    FD_CLR(clients[i].socket, &master); // remove from master set
    memset(&clients[i], 0, sizeof(ClientInfo_t));
}
/*
 send_invalid_command: send the invalid command to one client
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @parem err_message: the error message
 */
int send_invalid_command(int s, const char* err_message)
{
    InvalidCommand_t invalid;
    invalid.reply_type = INVALID;
    invalid.reply_string_size = (uint8_t)(strlen(err_message));
    invalid.reply_string = err_message;
    
    int len_str = (int)strlen(err_message);
    int len =sizeof(uint8_t)*2+ len_str+1;
    char* buf = (char*)malloc(len);
    char* rbuf = buf;
    memcpy(buf, &invalid.reply_type, sizeof(uint8_t));
    buf++;
    memcpy(buf, &invalid.reply_string_size, sizeof(uint8_t));
    buf++;
    
    memcpy(buf, err_message, len_str);
    buf+=len_str;
    *buf = '\n';
    if(send_all(s, rbuf, &len) == -1)
    {
        free(rbuf);
        perror("send");
        return -1;
    }
    free(rbuf);
    return 0;
}

/*
 send_welcome: send welcome package to one client
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @return: 0 for success and -1 for failure
 */
int send_welcome(int s)
{
    int len = sizeof(uint8_t) + sizeof(uint16_t)+1;
    
    char* buf = (char*)malloc(len);
    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = WELCOME;
    buf++;
    buf+=sizeof(uint16_t);
    *buf = '\n';
    
    if(send_all(s, rbuf, &len) == -1)
    {
        free(rbuf);
        perror("send");
        return -1;
    }
    free(rbuf);
    return 0;
}

/*
 send_announce: send announce package to one client
 */
// TODO
int send_announce(int s, const char* buf)
{
    /*Announce_t an = {(uint8_t)(WELCOME), (uint16_t)(0)};
    int len = sizeof(Welcome_t);
    if((send_all(s, &wel, &len)))
    {
        perror("send");
        return -1;
    }*/
    return 0;
}


/*
 parse_and_send: parse the input from client and send reply to clients
 @return: 0 for success, 1 for error
 @param s: the socket
 @param buf: the buffer
 */
int parse_and_send(int s, const char* buf)
{
    //fflush(stdout);
    //fflush(stderr);
#ifdef DEBUG
    //fprintf(stderr, "Client data: %s\n", buf);
#endif
    // Do nothing if the buffer is empty
    if(buf == NULL)
        return 0;
    
    uint8_t cmd = *((uint8_t*)buf);
    //Control_t td;
    
    buf++;
    if(cmd == HELLO_CMD)
    {
        uint16_t* tmp = (uint16_t*)buf;
        uint16_t port = (uint16_t)ntohs(*tmp);
       int index = find_client(s);
#ifdef DEBUG
        fprintf(stderr, "I get hello from socket %d, udp_port:%d.\n", s,port);
#endif
        
        if(clients[index].state == HANDSHAKED)
        {
            fprintf(stderr, "Already hand shaked.\n");
            if(send_invalid_command(s, "Already hand shaked.") == -1)
            {
                return -1;
            }
            //send_welcome(s);
        }else
        {
            // send welcome
            if(send_welcome(s) == -1)
            {
                return -1;
            }
            else
            {
                
                clients[index].state = HANDSHAKED;
                // convert to
                char udp_port[16];
                char* udp_port_ptr = udp_port;
                sprintf(udp_port_ptr, "%d", (int)port);
                //udp_port_ptr = itoa ((int)udp_port, udp_port_ptr, 10);
                // build the udp connection
                fprintf(stdout, "snowcast_server: build udp connection...\n");
                if(build_udpconnection(udp_port_ptr, index) == -1)
                {
                    fprintf(stderr,"snowcast_server: cannot build udp connection\n.\n");
                }
                else
                {
                    fprintf(stdout, "snowcast_server: done!\n");
#ifdef DEBUG
                    clients[index].udp_port = port;
#endif
                }
            }
        }
    }else if(cmd == SETSTATION_CMD)
    {
        
#ifdef DEBUG
        fprintf(stderr, "I get SetStation from socket %d.\n", s);
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
    
	while(1)
    {
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
                            clients[first].state = INIT_STATE;
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
                    memset(buf,0,BUF_SIZE);
					// else handle data from a client
					if (/*(nbytes = recv(i, buf, sizeof(uint8_t) + sizeof(uint16_t), 0)) <= 0*/
                        (nbytes = read_line(i, buf, BUF_SIZE-1))<=0
                        )
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
                        close_client(index,master);
                    }
					else // send to parser
					{
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
    //printf("%d",'\n');
    const char* tcp_port;
	if(argc < 2)
	{
		usage();
		exit(1);
	}
	else
	{
		tcp_port = argv[1];
        for(int i = 2; i < argc; i++)
		{
			song_files[i-2] = argv[i];
		}
        init_songs(song_files, argc-2);
	}

    int socket = 0;
    
    if((socket = init_listen(tcp_port, NULL)) < 0)
    {
        fprintf(stderr, "error binding socket.\n");
        exit(1);
    }
    
    init_server_locl();
    init_clients_info();
    init_stations();
    
    if(server_listen(socket) == -1)
    {
        fprintf(stderr, "error in server.");
        exit(1);
    }
    
    close(socket);
    release_stations();
	return 0;
}
