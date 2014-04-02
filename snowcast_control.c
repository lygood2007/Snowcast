/*
 Author: yanli, xiaoyi
 Time stamp: 03/31/2014
 */

// Macro for debug
#define DEBUG
#define TEST


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
#include <assert.h>
#include "command_queue.h"

/* defines */
#define HELLO_CMD 0
#define SETSTATION_CMD 1
#define REQUEST_CMD 2

#define WELCOME 0
#define ANNOUNCE 1
#define INVALID 2
#define REQUEST 3

#define ALL 0
#define CURRENT 1
#define ALL_STATION 2

#define BUF_SIZE 256 // 256 bytes
#define MAX_TOKENS 10
/* global variables for thread id */
pthread_t g_sender;
pthread_attr_t g_send_attr;

pthread_t g_receiver;
pthread_attr_t g_receiver_attr;
pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
command_queue_t g_cmd_queue;
int wait_for_announce = 0;

/* pending command queue */
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
int send_all(int s, const char *buf, int *len)
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

/*
 send_set_station: send the set_station command to server
 @return: 0 for success and -1 for failure
 @param s: socket
 @param station: the station number
 */
int send_set_station(int s, const char* station)
{
#ifdef DEBUG
    //fprintf(stdout, "\nI'm sending [Set %s].",station);
#endif
    
    uint16_t station_num = (uint16_t)atoi(station);
    station_num = htons(station_num);
    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, SETSTATION_CMD);
    
    pthread_mutex_unlock(&g_queue_mutex);
    
    int len = sizeof(uint8_t) + sizeof(uint16_t) + 1;
    char* buf = malloc(len);
    if(buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = SETSTATION_CMD;
    buf++;
    memcpy(buf, &station_num,sizeof(uint16_t));
    buf+= sizeof(uint16_t);
    *buf = '\n';// boundary
    if(send_all(s, rbuf, &len) == -1)
    {
        // Don't forget to release it
        free(rbuf);
        perror("send");
        return -1;
    }
    // Don't forget to release it
    free(rbuf);
    return 0;
}

/*
 send_request: send the request to the server
 @param s: the socket
 @param type: request type
 @return: 0 for success and -1 for failure
 */
int send_request(int s, uint8_t type)
{
    assert(type == ALL || type == CURRENT || type == ALL_STATION);
    int len = sizeof(uint8_t) + sizeof(uint8_t) + 1;
    char* buf = malloc(len);
    if(buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, REQUEST_CMD);
    
    pthread_mutex_unlock(&g_queue_mutex);
    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = REQUEST_CMD;
    
    buf++;
    memcpy(buf, &type,sizeof(uint8_t));
    
    buf+= sizeof(uint8_t);
    *buf = '\n';
    if(send_all(s, rbuf, &len) == -1)
    {
        // Don't forget to release it
        free(rbuf);
        perror("send");
        return -1;
    }
    // Don't forget to release it
    free(rbuf);
    return 0;
}

/*
 send_hello: send the hello command with udp port to server.
 @return: -1 for failure, 0 for success.
 @param s: the socket.
 @param buf: the buffer.
 @param len: the length of the buffer sent.
 */
int send_hello(int s, const char* udp_port){
    assert(s > 0);
    
    if(udp_port == NULL || strlen(udp_port) == 0)
        return -1;
    
    uint16_t udp_port_16 = (uint16_t)atoi(udp_port);
    udp_port_16 = (uint16_t)(htons(udp_port_16));
    // send bytes
    
    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, HELLO_CMD);

    pthread_mutex_unlock(&g_queue_mutex);
    int len = sizeof(uint16_t) + sizeof(uint8_t) + 1;
    char* buf = malloc(len);
    if(buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = HELLO_CMD;
    buf++;
    memcpy(buf, &udp_port_16,sizeof(uint16_t));
    buf+= sizeof(uint16_t);
    *buf = '\n';// boundary
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
 parse_and_send: parse the user input and then send
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @parem buf: the buffer from user input
 */
int parse_and_send(int s, char* buf)
{
    assert(s>0);
    if(buf == NULL)
        return 0;
    char * pch;
    pch = strtok (buf," \t");
    char* tokens[MAX_TOKENS];
    int num = 0;
    while (pch != NULL)
    {
        if(num == MAX_TOKENS)
            break;
        tokens[num++] = pch;
        pch = strtok (NULL, " \t");
    }
    if(num == 0)
    {
        return 0;
    }else if(num == 1)
    {
        if(strcmp(tokens[0], "Exit") == 0)
        {
            fprintf(stdout, "Thanks for using snowcast.\n");
            exit(0);
        }
        else if(strcmp(tokens[0], "Reqall") == 0)
        {
            // request all stations' current playing song
            send_request(s, ALL);
        }
        else if(strcmp(tokens[0], "Reqast") == 0)
        {
            // TODO
            send_request(s, ALL_STATION);
        }
        else if(strcmp(tokens[0], "Reqcur") == 0)
        {
            // TODO
            send_request(s, CURRENT);
        }
    }else
    {
        if(strcmp(tokens[0], "Set") == 0)
        {
            if(num != 2)
            {
                fprintf(stderr, "Ambiguous input.\n");
                //fprintf(stderr, "\nSet [station number]");
                return -1;
            }
            
            if(wait_for_announce == 1)
            {
                fprintf(stderr, "Waiting for server's last confirmation...\n");
                //fprintf(stderr, "\nSet [station number]");
                return -1;
            }
            if(send_set_station(s, tokens[1]) == -1)
            {
                fprintf(stderr, "Set station failed.\n");
                return -1;
            }else
            {
                // print
                fprintf(stdout, "Waiting for server's confirmation...\n");
                return 0;
            }
            
        }
    }
    fprintf(stderr, "Ambiguous input.\n");
    return -1;
}

void command_helper()
{
    fprintf(stdout, "\n*************************************\n");
    fprintf(stdout, "****************manual***************\n");
    fprintf(stdout, "/*************************************\n");
    fprintf(stdout, "Set [number]: set the station number.\n");
    fprintf(stdout, "Exit: disconnect.\n");
    // TODO
    /* Note that calling exit here doesn't mean the listner is killed */
    /* you have to kill it manually, a good way is to send the listner */
    fprintf(stdout, "**************************************/\n\n");
}
/*
 receiver_parser: parse the data coming from the server
 @param buf: the buffer
 */
void receiver_parser(const char* buf)
{
    if(buf == NULL)
        return;
    uint8_t first_byte = *(uint8_t*)buf;
    buf++;
    
    if(first_byte == WELCOME)
    {
        pthread_mutex_lock(&g_queue_mutex);
        uint8_t head = command_queue_head(&g_cmd_queue);
        if(head == HELLO_CMD)
        {
            fprintf(stdout, "\nSnowcast server: welcome, please set the station number.\n");
            command_helper();
            fputs("%%", stdout);
            fflush(stdout);
            command_queue_dequeue(&g_cmd_queue);
        }
        else
            assert(0);
        pthread_mutex_unlock(&g_queue_mutex);
    }else if(first_byte == ANNOUNCE)
    {
        // to be added
        uint8_t str_size = *(uint8_t*)buf;
        buf++;
        char tmp[BUF_SIZE];
        // copy the string
        int j;
        for(j = 0; j < str_size; j++)
        {
            tmp[j] = buf[j];
        }
        tmp[j] = '\0';
        if(strcmp(tmp, "Set Station Confirmed") == 0)
        {
            pthread_mutex_lock(&g_queue_mutex);
            uint8_t head = command_queue_head(&g_cmd_queue);
            if(head == SETSTATION_CMD)
            {
                wait_for_announce = 0;
                fprintf(stdout, "\nSnowcast server: Set station confirmed.\n");
                command_queue_dequeue(&g_cmd_queue);
            }
            else
                assert(0);
            pthread_mutex_unlock(&g_queue_mutex);
        }
        else if(strcmp(tmp, "Request Confirmed") == 0)
        {
            pthread_mutex_lock(&g_queue_mutex);
            uint8_t head = command_queue_head(&g_cmd_queue);
            if(head == REQUEST_CMD)
            {
                fprintf(stdout, "\nSnowcast server: Request confirmed.\n");
                command_queue_dequeue(&g_cmd_queue);
            }
            else
                assert(0);
            
            pthread_mutex_unlock(&g_queue_mutex);
        }
        else
        {
            // random announcement
            fprintf(stdout, "Snowcast server: %s\n",tmp);
        }
        
    }else if(first_byte == INVALID)
    {
        pthread_mutex_lock(&g_queue_mutex);
        uint8_t head = command_queue_head(&g_cmd_queue);
        if(head == SETSTATION_CMD)
        {
            wait_for_announce = 0;
        }
        uint8_t str_size = *(uint8_t*)buf;
        buf++;
        
        char tmp[BUF_SIZE];
        int j;
        for(j = 0;j < str_size; j++)
        {
            tmp[j] = buf[j];
        }
        tmp[j] = '\0';
        fprintf(stdout, "\nSnowcast server: %s\n",tmp);
        fputs("%%", stdout);
        fflush(stdout);
        command_queue_dequeue(&g_cmd_queue);
        pthread_mutex_unlock(&g_queue_mutex);
    }
    else
    {
        command_queue_dequeue(&g_cmd_queue);
        pthread_mutex_unlock(&g_queue_mutex);

    }
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
        //char* format = "\n%%";
        fputs("%%",stdout);
        fgets(buf, BUF_SIZE, stdin);
        // check if the buf end with '\n'
        int len = strlen(buf);
        if(buf[len-1] == '\n')
        {
            buf[len-1] = '\0';
            len--;
        }
#ifdef DEBUG
       // if(len > 0)
      //      //fprintf(stdout, "Buf:%s\n",buf);
#endif
        parse_and_send(sockfd, buf);
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
#ifdef DEBUG
    char test_buf[BUF_SIZE];
   // const char* test_buf_ptr = test_buf;
#endif
    while(1)
    {
        memset(buf, 0, BUF_SIZE);
        if ((numbytes = read_line(sockfd, buf,BUF_SIZE-1)) <= 0) {
			// Error when receive
            if(numbytes == 0)
            {
                fprintf(stdout, "Closing connection.\n");
            }else
            {
                perror("recv");
            }
            exit(1);
		}
#ifdef DEBUG
        snprintf(test_buf, BUF_SIZE-1, "Recv:%s\n",buf);
        //fprintf(stdout, "%s",test_buf_ptr);
#endif
        //fprintf(stderr, "bytes:%d\n",numbytes);
        receiver_parser(buf);
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
        //fprintf(stderr, "\nGetaddrinfo: %s", gai_strerror(rv));
        return -1;
	}
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
	{
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
							 p->ai_protocol)) == -1)
		{
            perror("socket");
			continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
            close(sockfd);
            perror("connect");
            continue;
		}
		break;
	}
    if (p == NULL)
	{
        fprintf(stderr, "Client: failed to connect\n");
        return -1;
	}
	freeaddrinfo(servinfo); // all done with this structure
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			  s, sizeof s);
    fprintf(stdout,"Client: connecting to %s\n", s);
    
    // send hello now
    if(send_hello(sockfd, udp_port) == -1)
    {
        fprintf(stderr, "Send hello failed\n");
        exit(1);
    }
    return sockfd;
}

// usage
void usage()
{
	fprintf(stderr, "\nSnowcast_control [server_name] [port] [udpport]");
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
    command_queue_init(&g_cmd_queue);
    int sockfd = open_connection(tcp_port, udp_port, server_name);
    if(sockfd == -1)
    {
        exit(1);
    }
    
    // initialize the queue
    
    // initialize the attribute
    pthread_attr_init(&g_send_attr);
    pthread_attr_init(&g_receiver_attr);
    
    // 1MB stack size
    pthread_attr_setstacksize(&g_send_attr, 1024*1024);
    pthread_attr_setstacksize(&g_receiver_attr, 1024*1024);
    
    pthread_create(&g_sender, &g_send_attr, send_message,(void*)((long)sockfd));
    pthread_create(&g_receiver, &g_receiver_attr, recv_message, (void*)((long)sockfd));
    
    pthread_join(g_sender, &ret);
    pthread_join(g_receiver, &ret);
    
    pthread_attr_destroy(&g_receiver_attr);
    pthread_attr_destroy(&g_send_attr);
    pthread_mutex_destroy(&g_queue_mutex);
    close(sockfd);
	return 0; 
}
