/*
 Source file: snowcast_control.c
 Brief: Implemented the functionality of client control over TCP protocol.
 Author: yanli (yan_li@brown.edu)
 Time stamp: 03/31/2014
 */

/* macros for switching to debug */
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
#include <assert.h>
#include "command_queue.h"

/* COMMANDs */
#define CMD_HELLO 0
#define CMD_SETSTATION 1
#define CMD_REQUEST 2

/* MESSAGEs */
#define MSG_WELCOME 0
#define MSG_ANNOUNCE 1
#define MSG_INVALID 2
#define MSG_REQUEST 3

/* TYPEs */
#define TYPE_ALL 0
#define TYPE_CURRENT 1
#define TYPE_ALL_STATION 2

#define BUF_SIZE 256 /* 256 bytes for buffer size */
#define MAX_TOKENS 10 /* Maximum token number */

/* global variables for thread id */
pthread_t g_sender; /* sender thread */
pthread_attr_t g_send_attr; /* sender thread's attribute */

pthread_t g_receiver; /* receiver thread */
pthread_attr_t g_receiver_attr; /* receiver thread's attribute */
pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER; /* mutex for queue */
command_queue_t g_cmd_queue; /* this is for storing the pending command queue */
int wait_for_announce = 0; /* a flag for marking it's waiting announce or not */

/* forward declaration */
void *get_in_addr(const struct sockaddr *sa);
int send_all(const int s, const char *buf, int *len);
int send_set_station(const int s, const char* station);
int send_request(const int s, const uint8_t type);
int send_hello(const int s, const char* udp_port);
int read_line(const int fd, char* data, const int maxlen);
int parse_and_send(int s, char* buf);
void command_helper();
void receiver_parser(const char* buf);
void* send_message(void* sockets);
void* recv_message(void* sockets);
int open_connection(const char* tcp_port, const char* udp_port,
                    const char* server_name);
void usage();

/* 
 get_in_addr: get sockaddr, IPv4 or IPv6.
 @param *sa: a pointer to socket address structure.
 */
void *get_in_addr(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


/*
 send_all: send the message in multiple passes.
 @return: -1 for failure, 0 for success.
 @param s: the socket.
 @param *buf: the buffer.
 @param *len: the length of the buffer sent.
 */
int send_all(const int s, const char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n = 0;
    while (total < *len)
	{
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1)
			break;

        total += n;
        bytesleft -= n;
	}
    *len = total; /* return number actually sent here */
    return n == -1 ? -1 : 0; /* return -1 on failure, 0 on success */
}

/*
 send_set_station: send the set_station command to server
 @return: 0 for success and -1 for failure
 @param s: the socket id
 @param *station: the pointer to the station number
 */
int send_set_station(const int s, const char* station)
{
#ifdef DEBUG
    fprintf(stdout, "\nI'm sending [Set %s].", station);
#endif
    
    uint16_t station_num = (uint16_t)atoi(station);
    station_num = htons(station_num);

    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, CMD_SETSTATION);
    pthread_mutex_unlock(&g_queue_mutex);
    
    int len = sizeof(uint8_t) + sizeof(uint16_t) + 1;
    char* buf = malloc(len);

    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }

    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = CMD_SETSTATION;
    buf++;
    memcpy(buf, &station_num, sizeof(uint16_t));
    buf += sizeof(uint16_t);
    *buf = '\n';

    if (send_all(s, rbuf, &len) == -1)
    {
        free(rbuf);
        perror("send");
        return -1;
    }

    free(rbuf);
    return 0;
}

/*
 send_request: send the request to the server
 @return: 0 for success and -1 for failure
 @param s: the socket id
 @param type: the request type
 */
int send_request(const int s, const uint8_t type)
{

#ifdef DEBUG
    assert(type == TYPE_ALL ||
           type == TYPE_CURRENT ||
           type == TYPE_ALL_STATION);
#endif

    int len = sizeof(uint8_t) + sizeof(uint8_t) + 1;
    char* buf = malloc(len);

    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, CMD_REQUEST);    
    pthread_mutex_unlock(&g_queue_mutex);

    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = CMD_REQUEST;
    
    buf++;
    memcpy(buf, &type, sizeof(uint8_t));
    
    buf += sizeof(uint8_t);
    *buf = '\n';

    if (send_all(s, rbuf, &len) == -1)
    {
        free(rbuf);
        perror("send");
        return -1;
    }

    free(rbuf);
    return 0;
}

/*
 send_hello: send the hello command with udp port to server.
 @return: -1 for failure, 0 for success.
 @param s: the socket id
 @param udp_port: the pointer to udp_port
 */
int send_hello(const int s, const char* udp_port)
{

#ifdef DEBUG    
    assert(s > 0);
#endif

    if (udp_port == NULL || strlen(udp_port) == 0)
        return -1;

    uint16_t udp_port_16 = (uint16_t)atoi(udp_port);
    udp_port_16 = (uint16_t)(htons(udp_port_16));
    
    pthread_mutex_lock(&g_queue_mutex);
    command_queue_enqueue(&g_cmd_queue, CMD_HELLO);
    pthread_mutex_unlock(&g_queue_mutex);

    int len = sizeof(uint16_t) + sizeof(uint8_t) + 1;
    char* buf = malloc(len);

    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }

    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = CMD_HELLO;
    buf++;
    memcpy(buf, &udp_port_16, sizeof(uint16_t));
    buf += sizeof(uint16_t);
    *buf = '\n';

    if (send_all(s, rbuf, &len) == -1)
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
 @return: the length in bytes that have been read,
          0 for disconnection, -1 for error
 @param fd: the file descriptor
 @param *data: the pointer to the buffer
 @param maxlen: the maximum length
 */
int read_line(const int fd, char* data, const int maxlen)
{
    int len = 0;
    while (len < maxlen)
    {
        char c;
        int ret = recv(fd, &c, 1, 0);
        if (ret == 0)
            return 0;
        else if (ret < 0)
            break;
        if (c == '\n')
        {
            data[len] = 0;
            return len;
        }
        data[len++] = c;
    }
    return -1;
}

/*
 parse_and_send: parse the user input and then send.
 @return: if succeed, return 0, else return -1.
 @param s: the socket id.
 @parem *buf: the pointer to the buffer from user input.
 */
int parse_and_send(int s, char* buf)
{
    assert(s>0);
    if (buf == NULL)
        return 0;
    char * pch;
    pch = strtok (buf," \t");
    char* tokens[MAX_TOKENS];
    int num = 0;
    while (pch != NULL)
    {
        if (num == MAX_TOKENS)
            break;
        tokens[num++] = pch;
        pch = strtok (NULL, " \t");
    }
    if (num == 0)
        return 0;
    else if (num == 1)
    {
        if (strcmp(tokens[0], "Exit") == 0)
        {
            fprintf(stdout, "Thanks for using snowcast.\n");
            exit(0);
        }
        else if (strcmp(tokens[0], "Reqall") == 0)
            send_request(s, TYPE_ALL);
        else if (strcmp(tokens[0], "Reqast") == 0)
            send_request(s, TYPE_ALL_STATION);
        else if (strcmp(tokens[0], "Reqcur") == 0)
            send_request(s, TYPE_CURRENT);
        else
            assert(0);
    }
    else
    {
        if (strcmp(tokens[0], "Set") == 0)
        {
            if (num != 2)
            {
                fprintf(stderr, "Ambiguous input.\n");
                return -1;
            }
            
            if (wait_for_announce == 1)
            {
                fprintf(stderr, "Waiting for server's last confirmation...\n");
                return -1;
            }
            if (send_set_station(s, tokens[1]) == -1)
            {
                fprintf(stderr, "Set station failed.\n");
                return -1;
            }
            else
            {
                fprintf(stdout, "Waiting for server's confirmation...\n");
                return 0;
            }
        }
    }
    fprintf(stderr, "Ambiguous input.\n");
    return -1;
}

/*
 command_helper: a helper function shows helps!
 */
void command_helper()
{
    fprintf(stdout, "\n*************************************\n");
    fprintf(stdout, "****************manual***************\n");
    fprintf(stdout, "/*************************************\n");
    fprintf(stdout, "Set [number]: set the station number.\n");
    fprintf(stdout, "Reqall: request all songs of your current station.\n");
    fprintf(stdout, "Reqcur: request the current cong of current station.\n");
    fprintf(stdout, "Reqast: request all stations' current playing song.\n");
    fprintf(stdout, "Exit: disconnect.\n");
    
    /* Note that calling exit here doesn't mean the listner is killed */
    /* you have to kill it manually, a good way is to send the listner */
    fprintf(stdout, "**************************************/\n\n");
}

/*
 receiver_parser: parse the data coming from the server
 @param *buf: the pointer to the buffer
 */
void receiver_parser(const char* buf)
{
    if (buf == NULL)
        return;

    uint8_t first_byte = *(uint8_t*)buf;
    buf++;
    
    if (first_byte == MSG_WELCOME)
    {
        pthread_mutex_lock(&g_queue_mutex);
        uint8_t head = command_queue_head(&g_cmd_queue);
        if (head == CMD_HELLO)
        {
            fprintf(stdout, 
                "\nSnowcast server: welcome, please set the station number.\n");
            command_helper();
            fputs("%%", stdout);
            fflush(stdout);
            command_queue_dequeue(&g_cmd_queue);
        }
        pthread_mutex_unlock(&g_queue_mutex);
    }else if (first_byte == MSG_ANNOUNCE)
    {
        uint8_t str_size = *(uint8_t*)buf;
        buf++;
        char tmp[BUF_SIZE] = {0};
        int j = 0;
        for (j = 0; j < str_size; j++)
        {
            tmp[j] = buf[j];
        }
        tmp[j] = '\0';

        if (strcmp(tmp, "Set Station Confirmed") == 0)
        {
            pthread_mutex_lock(&g_queue_mutex);
            uint8_t head = command_queue_head(&g_cmd_queue);
            
            if (head == CMD_SETSTATION)
            {
                wait_for_announce = 0;
                fprintf(stdout, "\nSnowcast server: Set station confirmed.\n");
                command_queue_dequeue(&g_cmd_queue);
            }

            pthread_mutex_unlock(&g_queue_mutex);
        }
        else if (strcmp(tmp, "Request Confirmed") == 0)
        {
            pthread_mutex_lock(&g_queue_mutex);
            uint8_t head = command_queue_head(&g_cmd_queue);
            
            if (head == CMD_REQUEST)
            {
                fprintf(stdout, "\nSnowcast server: Request confirmed.\n");
                command_queue_dequeue(&g_cmd_queue);
            }

            pthread_mutex_unlock(&g_queue_mutex);
        }
        else
        {
            /* random announcement */
            fprintf(stdout, "Snowcast server: %s\n",tmp);
        }
        
    }else if (first_byte == MSG_INVALID)
    {
        pthread_mutex_lock(&g_queue_mutex);

        uint8_t head = command_queue_head(&g_cmd_queue);
        
        if (head == CMD_SETSTATION)
            wait_for_announce = 0;

        uint8_t str_size = *(uint8_t*)buf;
        buf++;
        
        char tmp[BUF_SIZE];
        int j;
        for (j = 0;j < str_size; j++)
            tmp[j] = buf[j];
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
 @return: always NULL
 @param *sockets: the argument list
 */
void* send_message(void* sockets)
{
    char buf[BUF_SIZE] = {0};
    int sockfd = (int)(sockets);
    while (1)
    {
        fputs("%%",stdout);
        fgets(buf, BUF_SIZE, stdin);

        int len = strlen(buf);
        if (buf[len-1] == '\n')
        {
            buf[len-1] = '\0';
            len--;
        }

#ifdef DEBUG
    if (len > 0)
        fprintf(stdout, "Buf:%s\n", buf);
#endif

        parse_and_send(sockfd, buf);
    }
    
    return NULL;
}

/*
 recv_message: the thread for handling received message
 @return: nothing to return, always NULL
 @param *sockets: the argument list
 */
void* recv_message(void* sockets)
{
    int sockfd = (int)sockets;
    int numbytes = 0;
    char buf[BUF_SIZE] = {0};

#ifdef DEBUG
    char test_buf[BUF_SIZE] = {0};
#endif

    while (1)
    {
        memset(buf, 0, BUF_SIZE);
        if ((numbytes = read_line(sockfd, buf, BUF_SIZE-1)) <= 0) 
        {
            if (numbytes == 0)
                fprintf(stdout, "Closing connection.\n");
            else
                perror("recv");

            exit(EXIT_FAILURE);
		}
#ifdef DEBUG
        snprintf(test_buf, BUF_SIZE-1, "Recv:%s\n",buf);
#endif
        receiver_parser(buf);
    }
    return NULL;
}

/*
 open_connection: open the connection to music server.
 @return: socket number or -1 for error.
 @param *tcp_port: the tcp port number.
 @param *udp_port: the udp port number.
 @param *server_name: the server's IP address or name.
 */
int open_connection(const char* tcp_port, const char* udp_port,
                    const char* server_name)
{
    int sockfd = 0;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    int rv = 0;
    char s[INET6_ADDRSTRLEN] = {0};
    memset(s, 0, sizeof(s));
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(server_name, tcp_port, &hints, &servinfo)) != 0)
        return -1;
    
    /* loop through all the results and connect to the first we can */
    for (p = servinfo; p != NULL; p = p->ai_next)
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
    
    /* send hello message */
    if (send_hello(sockfd, udp_port) == -1)
    {
        fprintf(stderr, "Send hello failed\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

/* 
 usage: print the usage of this application
 */
void usage()
{
	fprintf(stderr, "\nSnowcast_control [server_name] [port] [udpport]\n");
}

int main(int argc, char *argv[])
{
    const char* tcp_port = NULL;
    const char* udp_port = NULL;
    const char* server_name = NULL;
    void* ret = NULL;

	if (argc != 4)
    {
        usage();
        exit(EXIT_FAILURE);
	}
    else
	{
		server_name = argv[1];
		tcp_port = argv[2];
		udp_port = argv[3];
	}

    /* initialize command queue */
    command_queue_init(&g_cmd_queue);

    /* open the connection */
    int sockfd = open_connection(tcp_port, udp_port, server_name);
    if (sockfd == -1)
        exit(EXIT_FAILURE);
    
    /* initialize the attribute for both threads */
    pthread_attr_init(&g_send_attr);
    pthread_attr_init(&g_receiver_attr);
    
    /* allocate 1MB stack size for both threads */
    pthread_attr_setstacksize(&g_send_attr, 1024*1024);
    pthread_attr_setstacksize(&g_receiver_attr, 1024*1024);
    
    /* Start the threads */
    pthread_create(&g_sender, &g_send_attr,
                   send_message,(void*)((long)sockfd));
    pthread_create(&g_receiver, &g_receiver_attr,
                   recv_message, (void*)((long)sockfd));
    
    /* Wait for the thread to finish */
    pthread_join(g_sender, &ret);
    pthread_join(g_receiver, &ret);
    
    /* Destroy the attributes */
    pthread_attr_destroy(&g_receiver_attr);
    pthread_attr_destroy(&g_send_attr);

    /* Destroy the mutex */
    pthread_mutex_destroy(&g_queue_mutex);

    /* Close the sockets */
    close(sockfd);

	return 0; 
}
