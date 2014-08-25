/*
 Source file: snowcast_server.c
 Brief: Everything about the server is in this source file. I used
        coarse-grained lock for simplicity when dealing with multiple threads.
        I also used linked list to represent the stations
 Author: yanli (yan_li@brown.edu)
 Time stamp: 03/31/2014
 */

/* macro for debug */
#define DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
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
#include <sys/time.h>

/* 
   The following macros, should be the same as snowcast_control. 
   for simplicity, I didn't use a common header since server and client
   are two different programs
 */
/* COMMANDs */
#define CMD_HELLO 0
#define CMD_SETSTATION 1
#define CMD_REQUEST 2

/* MESSAGEs */
#define MSG_WELCOME 0
#define MSG_ANNOUNCE 1
#define MSG_INVALID 2
#define MSG_REQUEST 3

#define TYPE_ALL 0
#define TYPE_CURRENT 1
#define TYPE_ALL_STATION 2

#define MAX_STATION_NUM 8
#define SONG_GROUPS 4
#define MAX_STATION_SONG_NUM 10
#define MAX_TOKENS 10

#define BACKLOG 10   /* how many pending connections queue will hold */
#define BUF_SIZE 1024 /* 1024 bytes */

#define MAX_CLIENT_NUM 10 /* we can at most get 10 clients */

typedef struct control
{
    uint8_t cmd;
    uint16_t info;
}control_t;

typedef struct welcome
{
    uint8_t reply_type;
    uint16_t num_stations;
}welcome_t;

typedef struct announce
{
    uint8_t reply_type;
    uint8_t songname_size;
    const char* song_name;
}announce_t;

typedef struct invalidcommand
{
    uint8_t reply_type;
    uint8_t reply_string_size;
    const char* reply_string;
}invalidcommand_t;

typedef enum state
{
    NO_STATE,
    INIT_STATE,
    HANDSHAKED
}state_t;

struct station;

typedef struct clientinfo
{
    state_t state;
    int socket; /* for tcp */
    const char* ip_address; /* for tcp */
    uint16_t udp_port;
    int udp_sock; /* for udp */
    struct addrinfo* udp_addrinfo; /* for udp */
    struct station* cur_station;
    /* to be added.. */
    struct clientinfo* next_client;
    
}clientinfo_t;

typedef struct clientlist
{
    clientinfo_t* head_clients;
    clientinfo_t* tail_clients;
    int counter;
}clientlist_t;

typedef struct station
{
    const char* songs[MAX_STATION_SONG_NUM];
    int song_counter;
    const char* cur_song;
    int fd;
    struct station* next_station;
    pthread_t sender;
    int id;
    clientlist_t connected_clients;
    pthread_mutex_t lock;
    
}station_t;

typedef struct stationlist
{
    station_t* head_station;
    station_t* tail_station;
    int counter;
    /* very very coarse grainded lock */
    pthread_mutex_t lock;
    
}stationlist_t;

/* global variables */
clientinfo_t g_clients[MAX_CLIENT_NUM];
stationlist_t g_station_list;
pthread_t g_user = 0; /* thread for handling user input */
const char* g_song_files[MAX_CLIENT_NUM] = {0};

/* forward declarations */
char* state_to_string(const state_t st);
void *get_in_addr(const struct sockaddr *sa);
int send_all(int s, char *buf, int *len);
int read_line(int fd, char data[], int maxlen);
int build_udpconnection(const char* udp_port, int index);
int loop_song(station_t* station);
void* loop_song_thread(void* args);
void cleanup_files_handler(void* args);
void init_server_locl();
void init_clients_info();
int find_first_client();
int find_client(int s);
int init_listen(const char* tcp_port, const char* server_name);
int send_invalid_command(int s, const char* message);
int send_welcome(int s);
int send_announce(int s, const char* buf);
void send_cur_song(int s, station_t* station);
void send_all_station_cur_song(int s);
void send_station_songs(int s, station_t* station);
station_t* find_station_from_id(int id);
void remove_client_from_station(clientinfo_t* client);
void close_client(int i, fd_set* master);
void append_client_to_station(clientinfo_t* client, station_t* station);
int parse_and_send(int s, const char* buf);
int server_listen(int sockfd);
int is_directory(const char* path);
int is_end_mp3(const char* str);
station_t* alloc_station();
void push_station_to_list(station_t* stat);
void spawn_sender(station_t* station);
int read_song(const char* str, station_t* station);
int read_song_dir(const char* path, station_t* station);
void add_station(const char* file);
void read_station(char* argv[], const int count);
void print_stations();
void print_clients();
void command_helper();
void* user_input(void* arg);
void shutdown_all();
void usage();
void init_stations();
void remove_station(station_t* station);
void release_stations();
void init_globals();

/*
 state_to_string: convert the state to string
 @param st: the state
 @return: the result string
 */
char* state_to_string(const state_t st)
{
    if (st == NO_STATE)
    {
        return "NO_STATE";
    }
    else if (st == INIT_STATE)
    {
        return "INIT_STATE";
    }
    else if (st == HANDSHAKED)
    {
        return "HAND_SHAKED";
    }
    
    assert(0);
}

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
 @param s: the socket.
 @param buf: the buffer.
 @param len: the length of the buffer sent.
 @return: -1 for failure, 0 for success.
 */
int send_all(int s, char *buf, int *len)
{
    //fprintf(stderr, "bytes:%d\n",*len);
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;
    while (total < *len) 
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
 @param fd: the file descriptor
 @param data: the buffer
 @return: the length in bytes that have been read, or 0 for disconnection and -1 for error
 */
int read_line(int fd, char data[], int maxlen)
{
    int len = 0;
    while (len < maxlen)
    {
        char c;
        int ret = recv(fd, &c, 1,0);
        if (ret == 0)
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
 @return: 0 for success and -1 for failure
 @param udp_port: the pointer to the udp port
 @param index: index in the client array
 */
int build_udpconnection(const char* udp_port, int index)
{
    assert(g_clients[index].ip_address != NULL);
    assert(g_clients[index].socket != 0);
    assert(g_clients[index].state != NO_STATE);
    int sockfd = 0;
    int rv = 0;
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    g_clients[index].ip_address = "127.0.0.1";
    const char* ip_addr = g_clients[index].ip_address;
    if ((rv = getaddrinfo(ip_addr, udp_port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "\nsnowcast_server: getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    /* loop through all the results and make a socket */
    for (p = servinfo; p != NULL; p = p->ai_next)
     {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             IPPROTO_UDP)) == -1) 
        {
            perror("socket");
            continue;
        }
        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "\nsnowcast_server: failed to bind socket\n");
        return -1;
    }
    
    g_clients[index].udp_addrinfo = p;
    g_clients[index].udp_sock = sockfd;
    return 0;
}

/*
 loop_song: loop the song and send the udp package to g_clients if there are.
 @param fd: the file descriptor of the song
 */
int loop_song(station_t* station)
{
    if (station->song_counter == 0)
        return 0;

    int fd = 0;
    int song_counter = 0;
    
    pthread_cleanup_push(cleanup_files_handler, &fd);

    /* open and close is cancellation point */
    if ((fd = open(station->songs[0], O_RDONLY, 0))<0)
    {
        perror("open");
        return -1;
    }
    else
    {
        station->fd = fd;
        station->cur_song = station->songs[song_counter];
    }
    /* one byte one time*/
    struct timeval tm;
	int time1 = 0, time2 = 0;
    int num_bytes = 0;
    char buf[BUF_SIZE];
    char* buf_ptr = buf;
    
    /* dangerous here because if the thread is cancelled, */
    while (1)
    {
        int loop = 0;
        gettimeofday(&tm,NULL);
		time1 = tm.tv_sec*1000000 + tm.tv_usec;

        while (loop != 16)
        {
            memset(buf,0,BUF_SIZE);
            int ret = read(fd, buf_ptr, BUF_SIZE);
            if (ret < 0)
            {
                perror("read");
                exit(EXIT_FAILURE);
            }
            else if (ret == 0)
            {
                /* open the next file */
                close(fd);
                fd = 0;
                song_counter = (song_counter+1)%station->song_counter;
                if ((fd = open(station->songs[song_counter], O_RDONLY, 0))<0)
                {
                    perror("open");
                    return -1;
                }else
                {
                    station->fd = fd;
                    station->cur_song = station->songs[song_counter];
                }
                pthread_mutex_lock(&station->lock);
                for (clientinfo_t* start = station->connected_clients.head_clients;
                     start != NULL; start = start->next_client)
                {
                    
                    assert(start->state == HANDSHAKED);
                    assert(start->udp_sock != 0);
                    
                    if ((num_bytes = sendto(start->udp_sock, buf_ptr, ret, 0,
                                            start->udp_addrinfo->ai_addr,
                                            start->udp_addrinfo->ai_addrlen)) == -1) 
                    {
                        perror("sendto");
                    }
                }
                pthread_mutex_unlock(&station->lock);
                loop = 16;
                for (clientinfo_t* start = station->connected_clients.head_clients;
                     start != NULL; start = start->next_client)
                {
                    
                    assert(start->state == HANDSHAKED);
                    assert(start->socket != 0);
                    char tmp_buf[BUF_SIZE];
                    char* tmp_buf_ptr = tmp_buf;
                    sprintf(tmp_buf_ptr, "Next song:%s",station->cur_song);
                    send_announce(start->socket,tmp_buf_ptr);
                }
                continue;
            }
            pthread_mutex_lock(&station->lock);
            for (clientinfo_t* start = station->connected_clients.head_clients;
                 start != NULL; start = start->next_client)
            {
                
                assert(start->state == HANDSHAKED);
                assert(start->udp_sock != 0);
                
                if ((num_bytes = sendto(start->udp_sock, buf_ptr, ret, 0,
                                        start->udp_addrinfo->ai_addr,
                                        start->udp_addrinfo->ai_addrlen)) == -1)
                {
                    perror("sendto");
                }
            }
            pthread_mutex_unlock(&station->lock);
            loop++;
        }
        loop  = 0;
		gettimeofday(&tm, NULL);
		time2 = tm.tv_sec*1000000 + tm.tv_usec ;
		int time_elapsed = time2 - time1;
		if (time_elapsed <= 1000000)
        {
			usleep(1000000 - time_elapsed);
		}
        pthread_testcancel();
    }
    pthread_cleanup_pop(0);
    return 0;
}

/*
 cleanup_handler: the cleanup handler, mainly for closing the file
 */
void cleanup_files_handler(void* args)
{
    /* may not be good here (file already closed), modify later */
    int fd = *((int*)args);
    if (fd != 0)
        close(fd);
}

/*
 loop_song_thread: loop the song and send the udp package to g_clients.
 @param *args: the argument list
 */
void* loop_song_thread(void* args)
{
    /* explicitly set the cancellation to be deferred (though it's default) */
    int old_type = 0;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_type);
    
    station_t* station = (station_t*)args;
    assert(station);
    loop_song(station);
    return NULL;
}

/*
 init_g_clients_info: initialize the clien info
 */
void init_clients_info()
{
    memset(g_clients, 0, sizeof(g_clients));
}

/*
 find_first_client: find the first idle client
 @return: return the index of first valid client, if full return -1
 */
int find_first_client()
{
    int i;
    for (i = 0; i < MAX_CLIENT_NUM;i++)
    {
        if (g_clients[i].state == NO_STATE)
            return i;
    }
    return -1;
}

/*
 find_client: find the client
 @return: the index if succeed, or -1 for not found
 @param s: the socket number
 */
int find_client(int s)
{
    int i;
    for (i = 0; i < MAX_CLIENT_NUM;i++)
    {
        if (g_clients[i].socket == s)
        {
            return i;
        }
    }
    return -1;
}

/*
 init_listen: initialize the socket.
 @return: -1 for failure, 0 for success.
 @param tcp_port: the pointer to the tcp port.
 @param *server_name: the pointer to the server's name.
 */
int init_listen(const char* tcp_port, const char* server_name)
{
    struct addrinfo hints, *ai = NULL, *p = NULL;
    int rv = 0;
    int sockfd = 0;
    
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
    
	/* search for the services */
	for (p = ai; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd < 0)
			continue;
        
        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            freeaddrinfo(ai);
            exit(EXIT_FAILURE);
        }
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0)
		{
			close(sockfd);
			continue;
		}
        
		break;
	}

	if (p == NULL)
	{
		fprintf(stderr, "Snowcast_server: failed to bind\n");
        freeaddrinfo(ai);
		return -1;
	}
	freeaddrinfo(ai);
    return sockfd;
}


/*
 send_invalid_command: send the invalid command to one client
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @parem err_message: the error message
 */
int send_invalid_command(int s, const char* err_message)
{
    invalidcommand_t invalid;
    invalid.reply_type = MSG_INVALID;
    invalid.reply_string_size = (uint8_t)(strlen(err_message));
    invalid.reply_string = err_message;
    
    int len_str = (int)strlen(err_message);
    int len =sizeof(uint8_t)*2+ len_str+1;
    char* buf = (char*)malloc(len);
    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    char* rbuf = buf;
    memcpy(buf, &invalid.reply_type, sizeof(uint8_t));
    buf++;
    memcpy(buf, &invalid.reply_string_size, sizeof(uint8_t));
    buf++;
    
    memcpy(buf, err_message, len_str);
    buf+=len_str;
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
 send_welcome: send welcome package to one client
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @return: 0 for success and -1 for failure
 */
int send_welcome(int s)
{
    int len = sizeof(uint8_t) + sizeof(uint16_t)+1;
    
    char* buf = (char*)malloc(len);
    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    char* rbuf = buf;
    memset(buf, 0, len);
    *buf = MSG_WELCOME;
    buf++;
    buf+=sizeof(uint16_t);
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
 send_invalid_command: send the announce command to one client
 @return: if succeed, return 0, else return -1
 @param s: the socket
 @parem message: the message
 */
int send_announce(int s, const char* message)
{
    announce_t announce;
    announce.reply_type = MSG_ANNOUNCE;
    announce.songname_size = (uint8_t)(strlen(message));
    announce.song_name = message;
    
    int len_str = (int)strlen(message);
    int len =sizeof(uint8_t)*2+ len_str+1;
    char* buf = (char*)malloc(len);
    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    char* rbuf = buf;
    memcpy(buf, &announce.reply_type, sizeof(uint8_t));
    buf++;
    memcpy(buf, &announce.songname_size, sizeof(uint8_t));
    buf++;
    
    memcpy(buf, message, len_str);
    buf+=len_str;
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
 send_single_song: send the current song of the requested client's station.
 @param s: the socket
 @param station: the client's station
 */
void send_cur_song(int s, station_t* station)
{
    assert(station);
    char buf[BUF_SIZE] = {0};
    char* buf_ptr = buf;
    sprintf(buf_ptr, "current playing: %s", station->cur_song);
    send_announce(s, buf_ptr);
}

/*
 send_all_station_cur_cong: send the current song
 @param s: the socket
 */
void send_all_station_cur_song(int s)
{
    station_t* st = g_station_list.head_station;
    int i = 0;
    while (st != NULL)
    {
        char buf[BUF_SIZE];
        sprintf(buf, "station[%d]:%s",i,st->cur_song);
        char* buf_ptr = buf;
        send_announce(s, buf_ptr);
        st = st->next_station;
        i++;
    }
}

/*
 send_station_songs: send the specified station's all songs
 @param s: the socket
 @param station: the client's station
 */
void send_station_songs(int s, station_t* station)
{
    assert(station);
    for (int i = 0; i < station->song_counter; i++)
    {
        send_announce(s, station->songs[i]);
    }
}

/*
 find_station_from_id: find the station from the id
 @param id: the id
 @return: NULL for not existed.
 */
station_t* find_station_from_id(int id)
{
    pthread_mutex_lock(&g_station_list.lock);
    station_t* st = g_station_list.head_station;
    while (st != NULL)
    {
        if (st->id == id)
        {
            pthread_mutex_unlock(&g_station_list.lock);
            return st;
        }
        else
        {
            st = st->next_station;
        }
    }
    pthread_mutex_unlock(&g_station_list.lock);
    return NULL;
}

/*
 remove_client_from_station: remove the client from its current station
 @param client: the pointer to the client
 */
void remove_client_from_station(clientinfo_t* client)
{
    if (client->cur_station == NULL)
       return;
    pthread_mutex_lock(&client->cur_station->lock);
    clientinfo_t* c = client->cur_station->connected_clients.head_clients;   
    assert(c);
    station_t* station = client->cur_station;
    if (client == c)
    {

        station->connected_clients.head_clients = station->connected_clients.head_clients->next_client;
        if (station->connected_clients.tail_clients == client) // only one item
        {
            station->connected_clients.tail_clients = station->connected_clients.head_clients;
            assert(station->connected_clients.head_clients == NULL);
            assert(station->connected_clients.tail_clients == NULL);
            
        }
        pthread_mutex_unlock(&client->cur_station->lock);
        return;
    }
    while (c->next_client != NULL)
    {
        if (c->next_client == client)
        {
            c->next_client = client->next_client;
            if (c->next_client == NULL)
            {
                station->connected_clients.tail_clients = c;
            }
            
            pthread_mutex_unlock(&client->cur_station->lock);
            return;
        }
        else
        {
            c = c->next_client;
        }
    }
    pthread_mutex_unlock(&client->cur_station->lock);
}

/*
 close_client: close the socket and ret the structure
 @param i: the index
 @param master: the pointer to the master set
 */
void close_client(int i, fd_set* master)
{
    assert(i>=0 && i < MAX_CLIENT_NUM);
    assert(g_clients[i].socket != 0);
    close(g_clients[i].socket);
    assert(g_clients[i].udp_sock != 0);
    close(g_clients[i].udp_sock);
    
    FD_CLR(g_clients[i].socket, master);
    remove_client_from_station(&g_clients[i]);
    
    memset(&g_clients[i], 0, sizeof(clientinfo_t));
    assert(g_clients[i].state == NO_STATE);
}
/*
 append_client_to_station: append the client to the destination station
 @param client: the client
 @param station: the target station
 */
void append_client_to_station(clientinfo_t* client, station_t* station)
{
    assert(client);
    assert(station);
    pthread_mutex_lock(&station->lock);
    if (client->cur_station == station)
    {
        pthread_mutex_unlock(&station->lock);
        return;
    }
    if (station->connected_clients.head_clients == NULL)
    {
        station->connected_clients.head_clients = client;
        station->connected_clients.tail_clients = client;
    }
    else
    {
        station->connected_clients.tail_clients->next_client = client;
        station->connected_clients.tail_clients = station->connected_clients.tail_clients->next_client;
    }
    station->connected_clients.counter++;
    client->cur_station = station;
    pthread_mutex_unlock(&station->lock);
}

/*
 parse_and_send: parse the input from client and send reply to g_clients
 @return: 0 for success, 1 for error
 @param s: the socket
 @param buf: the buffer
 */
int parse_and_send(int s, const char* buf)
{
    if (buf == NULL)
        return 0;
    
    uint8_t cmd = *((uint8_t*)buf);
    
    buf++;
    if (cmd == CMD_HELLO)
    {
        uint16_t* tmp = (uint16_t*)buf;
        uint16_t port = (uint16_t)ntohs(*tmp);
       int index = find_client(s);

#ifdef DEBUG
        fprintf(stderr, "I get hello from socket %d, udp_port:%d.\n", s,port);
#endif
        
        if (g_clients[index].state == HANDSHAKED)
        {
            if (send_invalid_command(s, "Already hand shaked.") == -1)
            {
                return -1;
            }
        }
        else
        {
            if (send_welcome(s) == -1)
            {
                return -1;
            }
            else
            {
                g_clients[index].state = HANDSHAKED;
                char udp_port[16] = {0};
                char* udp_port_ptr = udp_port;
                sprintf(udp_port_ptr, "%d", (int)port);
                /* build the udp connection */
                fprintf(stdout, "\nsnowcast_server: build udp connection...\n");
                if (build_udpconnection(udp_port_ptr, index) == -1)
                {
                    fprintf(stderr,"snowcast_server: cannot build udp connection\n.\n");
                }
                else
                {
                    fprintf(stdout, "snowcast_server: done!\n");
                    g_clients[index].udp_port = port;
                }
            }
        }
    }
    else if (cmd == CMD_SETSTATION)
    {
        int index = find_client(s);
        if (g_clients[index].state == INIT_STATE)
        {
            if (send_invalid_command(s, "Not yet hand shaked.") == -1)
            {
                return -1;
            }
        }

#ifdef DEBUG
        fprintf(stderr, "I get Setstation from socket %d.\n", s);
#endif

        char station[16] = {0};
        char* station_ptr = station;
        
        uint16_t* tmp = (uint16_t*)buf;
        uint16_t station_num = (uint16_t)ntohs(*tmp);
        
        sprintf(station_ptr, "%d", (int)station_num);

#ifdef DEBUG
        fprintf(stdout, "I get [Set %d].\n", station_num);
#endif

        station_t* target = find_station_from_id(station_num);
        
        if (target == NULL)
        {
            send_invalid_command(s, "Invalid station number.");
            return 0;
        }
        
        if (g_clients[index].cur_station != NULL)
        {
            remove_client_from_station(&g_clients[index]);
        }

        append_client_to_station(&g_clients[index], target);
        
        send_announce(g_clients[index].socket, "Set Station Confirmed");
        send_cur_song(s,g_clients[index].cur_station );
        
    }
    else if (cmd == CMD_REQUEST)
    {     
        uint8_t* tmp = (uint8_t*)buf;
        uint8_t req_type = (*tmp);
        
        if (req_type == TYPE_ALL)
        {
            int index = find_client(s);
            if (g_clients[index].state == INIT_STATE)
            {
                if (send_invalid_command(s, "Not yet hand shaked.") == -1)
                {
                    return -1;
                }
            }
            if (g_clients[index].cur_station == NULL)
            {
                if (send_invalid_command(s, "Not yet set station.") == -1)
                {
                    return -1;
                }
            }
            else
            {
                send_announce(s, "Request Confirmed");
                send_station_songs(s, g_clients[index].cur_station);
            }
        }
        else if (req_type == TYPE_CURRENT)
        {
            int index = find_client(s);
            if (g_clients[index].state == INIT_STATE)
            {
                if (send_invalid_command(s, "Not yet hand shaked.") == -1)
                {
                    return -1;
                }
            }
            if (g_clients[index].cur_station == NULL)
            {
                if (send_invalid_command(s, "Not yet set station.") == -1)
                {
                    return -1;
                }
            }
            else
            {
                send_announce(s, "Request Confirmed");
                send_cur_song(s,g_clients[index].cur_station );
            }
        }
        else if (req_type == TYPE_ALL_STATION)
        {
            send_announce(s, "Request Confirmed");
            send_all_station_cur_song(s);
        }
    }
    return 0;
}

/*
 server_listen: the main loop for server.
 @return: -1 for failure, 0 for success.
 @param sockfd: the socket id.
 */
int server_listen(int sockfd)
{
    fprintf(stdout, "snowcast_server: start listening\n");

    if (listen(sockfd, BACKLOG) == -1)
    {
		perror("listen");
        return -1;
    }

    int i = 0;
    int fdmax = 0;
    fd_set master;
    fd_set read_fds;
    struct sockaddr_storage remoteaddr;
	socklen_t addrlen;
    char buf[BUF_SIZE] = {0};
    char remoteIP[INET6_ADDRSTRLEN] = {0};
    
    FD_ZERO(&master); 
	FD_ZERO(&read_fds);
	FD_SET(sockfd, &master);
	fdmax = sockfd;
    
	while (1)
    {
		read_fds = master;
        
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			return -1;
		}
		for (i = 0; i <= fdmax; i++)
		{
			if (FD_ISSET(i, &read_fds))
			{ 
                /* we got one!! */
				if (i == sockfd)
				{
					addrlen = sizeof(remoteaddr);
					int newfd = accept(sockfd,
								   (struct sockaddr *)&remoteaddr,
								   &addrlen);
					if (newfd == -1)
					{
						perror("accept");
                        return -1;
					} 
                    else
					{
						FD_SET(newfd, &master);
						if (newfd > fdmax)
							fdmax = newfd;

                        int first = find_first_client();
                        if (first == -1)
                        {
                            const char* messg = "Busy, close the connection";
                            send(newfd, messg, strlen(messg),0);
                            close(newfd);
                        }
                        else
                        {
                            /* We get a new client, push it into clientinfo */
                            g_clients[first].state = INIT_STATE;
                            g_clients[first].socket = newfd;
                            g_clients[first].ip_address = 
                            inet_ntop(remoteaddr.ss_family,
                                      get_in_addr((struct sockaddr*)&remoteaddr),
                                      remoteIP, INET6_ADDRSTRLEN);
                            fprintf(stdout,"\nsnowcast_server: new client from"
                                    " %s on socket %d\n",
                                    g_clients[first].ip_address,
                                    g_clients[first].socket);
                        }
					}
				} 
                else
				{
                    int nbytes = 0;
                    memset(buf,0,BUF_SIZE);
					/* else handle data from a client */
					if ((nbytes = read_line(i, buf, BUF_SIZE-1)) <= 0)
					{
                        int index = find_client(i);
						/* got error or connection closed by client */
						if (nbytes == 0)
						{
                            fprintf(stdout,
                                    "\nsnowcast_server: disconnected from %s\n",
                                    g_clients[index].ip_address);
                            fprintf(stdout,
                                    "snowcast_server: socket %d hung up\n",
                                    i);

                            clientinfo_t* cl = &g_clients[index];
                            char buf[BUF_SIZE];
                            char* buf_ptr = buf;
                            strcpy(buf_ptr, "STOP");
                            int num_bytes = 0;
                            if ((num_bytes = sendto(cl->udp_sock, buf_ptr, 4, 0,
                                                    cl->udp_addrinfo->ai_addr,
                                                    cl->udp_addrinfo->ai_addrlen))
                                                    == -1)
                            {
                                perror("sendto");
                            }
                            
                        }
                        else
                            perror("recv");

                        close_client(index,&master);
                    }
					else
					{
                        parse_and_send(i, buf);
					}
                }
            }
        }
    }
}

/*
 is_dir: tell if path is a directory
 @return: 0 for not directory and 1 for directory
 @param path: the path
 */
int is_directory(const char* path)
{
    struct stat statbuf;
    if (lstat(path, &statbuf) < 0)
    {
        perror("lstat");
        exit(EXIT_FAILURE);
    }
    else if (S_ISDIR(statbuf.st_mode) == 1)
    {
        return 1;
    }
    return 0;
}

/*
 is_end_mp3: check if the string is ended with mp3
 @param str: the string
 @return: 1 for mp3 and 0 for not
 */
int is_end_mp3(const char* str)
{
    if (str == 0 || strlen(str) <= 4)
        return 0;
    
    const char* start = str+strlen(str)-4;
    if (strcmp(start, ".mp3") == 0)
        return 1;
    else
        return 0;
}

/*
 alloc_station: allocate the memory for station
 @return: the allocated memory address
 */
station_t* alloc_station()
{
    station_t* station = (station_t*)malloc(sizeof(station_t));

    if (station == NULL)
    {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
    
    memset(station, 0, sizeof(station_t));
    pthread_mutex_init(&station->lock,NULL);
    return station;
}

/*
 push_station_to_list: push the new station to the list
 @param *stat: the pointer to a station object
 */
void push_station_to_list(station_t* stat)
{
    pthread_mutex_lock(&g_station_list.lock);
    
    assert(stat);
    int next_available = 0;
    station_t* station = g_station_list.head_station;
    while (station != NULL)
    {
        if (station->id != next_available)
        {
            break;
        }
        station = station->next_station;
        next_available++;
    }
    stat->id = next_available;
    
    if (g_station_list.head_station == NULL)
    {
        g_station_list.head_station = stat;
        g_station_list.tail_station = stat;
    }
    else
    {
        g_station_list.tail_station->next_station = stat;
        g_station_list.tail_station = g_station_list.tail_station->next_station;
    }
    g_station_list.counter++;
    pthread_mutex_unlock(&g_station_list.lock);
}

/*
 spawn_sender: spawn a new sender thread for the station.
 @param station: pointer to the station
 */
void spawn_sender(station_t* station)
{
    assert(station != NULL);
    assert(station->sender == 0);
    
    pthread_create(&station->sender, NULL, loop_song_thread, (void*)station);
    pthread_detach(station->sender);
}

/*
 read_song: read the song into station
 @param str: the song name
 @param station: the station
 @return: 0 for success, -1 for failure, basically full.
 */
int read_song(const char* str, station_t* station)
{
    assert(is_end_mp3(str));

    if (station->song_counter < MAX_STATION_SONG_NUM)
    {
        char* new_song = (char*)malloc(sizeof(char)*BUF_SIZE);
        memset(new_song, 0, BUF_SIZE*sizeof(char));
        strcpy(new_song, str);
        station->songs[station->song_counter++] = new_song;
        
        return 0;
    }
    return -1;
}

/*
 read_song_dir: read the directory
 @param path: the path for the directory
 @param station: the new station
 @return: 0 for success and -1 for failure
 */
int read_song_dir(const char* path, station_t* station)
{
    assert(is_directory(path));

    struct dirent* dirp;
    DIR	*dp = NULL;
    char next_path[BUF_SIZE] = {0};
    strcpy(next_path, path);
    char* ptr = next_path;
    ptr+= strlen(next_path);
    *ptr++ = '/';
    *ptr = '\0';

    if ((dp = opendir(path)) == NULL)
    {
        perror("dp");
        return -1;
    }

    while ((dirp = readdir(dp)) != NULL) {
		if (strcmp(dirp->d_name, ".") == 0  ||
		    strcmp(dirp->d_name, "..") == 0)
            continue;
        else
        {
            if (dirp->d_name[0] == '.')
            {
                continue;
            }
            strcpy(ptr, dirp->d_name);
            
            if (is_directory(next_path))
            {
                int ret = read_song_dir(next_path, station);
                if (ret == -1)
                    return -1;
            }
            else if (is_end_mp3(next_path))
            {
                int ret = read_song(next_path, station);
                if (ret == -1)
                    return -1;
            }
            /* skip over non-mp3 files.. */
        }
    }
    if (closedir(dp)<0)
    {
        perror("closedir");
        return -1;
    }
    return 0;
}

/*
 add_station: add the station to the server
 @param file: the pointer to a target file name
 */
void add_station(const char* file)
{
    pthread_mutex_lock(&g_station_list.lock);
    if (g_station_list.counter == MAX_STATION_NUM)
    {
        pthread_mutex_unlock(&g_station_list.lock);
        fprintf(stderr, "Error: you are adding %s but server is full.\n", file);
        return;
    }
    pthread_mutex_unlock(&g_station_list.lock);
    station_t* new_station = alloc_station();
    assert(new_station);
    
    if (is_end_mp3(file))
    {
        read_song(file, new_station);
    }
    else if (is_directory(file))
    {
        read_song_dir(file, new_station);
    }
    
    if (new_station->song_counter == 0)
        free(new_station);
    else
    {
        push_station_to_list(new_station);
        /* spawn a new sender thread for the station */
        spawn_sender(new_station);
        fprintf(stdout, "Added station %s successfully.\n",file);
    }
}

/*
 read_station: the helper function for reading the directory
 @param argv: the argument list
 @param count: the size of the argument list
*/
void read_station(char* argv[], const int count)
{
    for (int i = 0; i < count; i++)
        add_station(argv[i]);
}

/*
 print_stations: print all of the stations for debug
 */
void print_stations()
{
    pthread_mutex_lock(&g_station_list.lock);
    station_t* start = g_station_list.head_station;
    if (start == NULL)
    {
        assert(g_station_list.counter == 0);
        fprintf(stdout, "There are %d stations.\n", g_station_list.counter);
        fprintf(stdout, "Empty!\n");
        pthread_mutex_unlock(&g_station_list.lock);
        return;
    }
    
    assert(g_station_list.counter > 0);
    fprintf(stdout, "There are %d stations.\n", g_station_list.counter);
    while (start != NULL)
    {    
        pthread_mutex_lock(&start->lock);
        for (int i= 0; i < start->song_counter; i++)
        {
            fprintf(stdout, "station[%d]: %s\n",start->id,start->songs[i]);
        }

        clientinfo_t* c = start->connected_clients.head_clients;
        while (c != NULL)
        {
            fprintf(stdout, "station[%d]: clients[%s:%d]\n",
                    start->id, c->ip_address, c->udp_port);
            c = c->next_client;
        }
        pthread_mutex_unlock(&start->lock);
        start = start->next_station;
    }
    pthread_mutex_unlock(&g_station_list.lock);
}

/*
 print_clients: print all of the information of clients.
 */
void print_clients()
{
    int empty = 1;
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        if (g_clients[i].state != NO_STATE)
        {
            fprintf(stdout,"clients[%d]: [state %s], [ip:%s],[udp port: %d]\n",
                    i, state_to_string(g_clients[i].state),
                    g_clients[i].ip_address, g_clients[i].udp_port);
            empty = 0;
        }
    }
    if (empty)
    {
        fprintf(stdout, "No clients.\n");
    }
}

/*
 command_helper: show the help manual.
 */
void command_helper()
{
    fprintf(stdout, "\n********************************************\n");
    fprintf(stdout, "****************manual**********************\n");
    fprintf(stdout, "l: list all stations.\n");
    fprintf(stdout, "c: print all clients.\n");
    fprintf(stdout, "q: exit.\n");
    fprintf(stdout, "a [dir/mp3]: add new station to your server.\n");
    fprintf(stdout, "d [number]: delete the station.\n");
    fprintf(stdout, "s: shutdown all stations.\n");
    fprintf(stdout, "********************************************\n\n");
}

/*
 user_input: the thread function for handling user input
 @return: always NULL
 @param arg: the argument of the thread
 */
void* user_input(void* arg)
{
    char user_input[BUF_SIZE] = {0};

    while (1)
    {
        fputs("\ninput:\n", stdout);
        memset(user_input, 0, sizeof(user_input));
        fgets(user_input, BUF_SIZE, stdin);
        int len = strlen(user_input);
        if (len > 0 && user_input[len-1] == '\n')
            user_input[len-1] = '\0';
        len = strlen(user_input);
        char * pch;
        pch = strtok (user_input," \t");
        char* tokens[MAX_TOKENS];
        int num = 0;
        while (pch != NULL)
        {
            if (num == MAX_TOKENS)
                break;
            tokens[num++] = pch;
            pch = strtok (NULL, " \t");
        };

        switch(user_input[0])
        {
            case 'h':
            {
                if (len != 1)
                {
                    fprintf(stderr, "Ambiguous command. Help for 'h'.\n");
                    continue;
                }
                command_helper();
                break;
            }
            case 'q':
            {
                if (len != 1)
                {
                    fprintf(stderr, "Ambiguous command. Help for 'h'.\n");
                    continue;
                }
                release_stations();
                print_stations();
                break;
            }
            case 'l':
            {
                if (len != 1)
                {
                    fprintf(stderr, "Ambiguous command. Help for 'h'.\n");
                    continue;
                }
                print_stations();
                break;
            }
            case 'c':
            {
                if (len != 1)
                {
                    fprintf(stderr, "Ambiguous command. Help for 'h'.\n");
                    continue;
                }
                print_clients();
                break;
            }
            case 'a':
            {
                if (strcmp(tokens[0], "a") != 0 || num != 2)
                {
                    fprintf(stderr, "Ambiguous input.\n");
                    continue;
                }
                add_station(tokens[1]);
                /* send an anouncement to all clients */
                for (int i = 0; i < MAX_CLIENT_NUM; i++)
                {
                    if (g_clients[i].state == HANDSHAKED)
                    {
                        send_announce(g_clients[i].socket,
                                      "New station is added.");
                    }
                }
                break;
            }
            case 'd':
            {
                if (strcmp(tokens[0],"d") != 0)
                {
                    fprintf(stderr, "Ambiguous input.\n");
                    continue;
                }

                for (int i = 1; i < num; i++)
                {
                    /* convert the tokens to real number */
                    int id = atoi(tokens[i]);
                    pthread_mutex_lock(&g_station_list.lock);
                    if (id < 0 || id >= g_station_list.counter)
                    {
                        pthread_mutex_unlock(&g_station_list.lock);
                        continue;
                    }
                    pthread_mutex_unlock(&g_station_list.lock);
                    station_t* station = find_station_from_id(id);
                    if (station == NULL)
                        continue;

                    remove_station(station);
                }
                break;
            }
            case 's':
            {
                shutdown_all();
                break;
            }
            default:
            {
                fprintf(stdout, "Ambiguous command. Help for 'h'.\n");
                continue;
            }
        }
    }
}

/*
 shutdown_all: shut down every station and close everything
 */
void shutdown_all()
{
    while (g_station_list.head_station != NULL)
        remove_station(g_station_list.head_station);
    
    /* make sure everything is cleared */
    assert(g_station_list.counter == 0);
    assert(g_station_list.head_station == NULL);
    assert(g_station_list.tail_station == NULL);
}

/*
 usage: show how to use the program
 */
void usage()
{
	fprintf(stdout, "snowcast_server [tcpport] [file1] [file2] ...\n");
}

/*
 init_stations: initialize the stations to be zero
 */
void init_stations()
{
    memset(&g_station_list, 0, sizeof(g_station_list));
    pthread_mutex_init(&g_station_list.lock, NULL);
    fprintf(stdout, "\n/**The server can have at most %d stations.**/\n",
            MAX_STATION_NUM);
}

/*
 remove_station: remove the specified station
 @param station: the station address
 */
void remove_station(station_t* station)
{
    assert(station != NULL);
    int find = 0;
    pthread_mutex_lock(&g_station_list.lock);
    station_t* st = g_station_list.head_station;
    if (station == st)
    {
        find = 1;
        /* CAUTION: I use pthread_cancel, I must take care of cancel points */
        station_t* station = st;
        pthread_cancel(station->sender);
        
        /* send a package firstly to all connect clients */
        pthread_mutex_lock(&station->lock);
        clientinfo_t* c = station->connected_clients.head_clients;
        while (c != NULL)
        {
            send_announce(c->socket, "Station is closed.");
            clientinfo_t* next = c->next_client;
            c->next_client = NULL;
            c->cur_station = NULL;
            c = next;
            station->connected_clients.counter--;
        }
        assert(station->connected_clients.counter == 0);
    
        /* release the station */
        for (int i = 0; i < station->song_counter; i++)
            free((void*)station->songs[i]);
        pthread_mutex_unlock(&station->lock);
        
        pthread_mutex_destroy(&station->lock);
        g_station_list.counter--;

        if (g_station_list.counter == 0)
        {
            g_station_list.head_station = NULL;
            g_station_list.tail_station = NULL;
        }
        else
            g_station_list.head_station = station->next_station;

        free(station);
    }
    else
    {
        while (st->next_station != NULL)
        {
            if (st->next_station == station)
            {
                find = 1;
                 station_t* station = st->next_station;
                /* CAUTION: I use pthread_cancel,
                   I must take care of the cancel point */
                pthread_cancel(station->sender);
                
                pthread_mutex_lock(&station->lock);
                clientinfo_t* c = station->connected_clients.head_clients;
                while (c != NULL)
                {
                    send_announce(c->socket, "Station is closed.");
                    clientinfo_t* next = c->next_client;
                    c->next_client = NULL;
                    c->cur_station = NULL;
                    c = next;
                    station->connected_clients.counter--;
                }
                assert(station->connected_clients.counter == 0);
                
                for (int i = 0; i < station->song_counter; i++)
                {
                    free((void*)station->songs[i]);
                }
                pthread_mutex_unlock(&station->lock);
                pthread_mutex_destroy(&station->lock);
                g_station_list.counter--;
                st->next_station = station->next_station;
                if (st->next_station == NULL)
                {
                    g_station_list.tail_station = st;
                }
                free(station);
                break;
            }
            else
            {
                st = st->next_station;
            }
        }
    }
    pthread_mutex_unlock(&g_station_list.lock);
    assert(find);
}

/*
 release_stations: wait for song threads to end
 */
void release_stations()
{
    pthread_mutex_lock(&g_station_list.lock);
    station_t* start = g_station_list.head_station;
    while (start != NULL)
    {
        /* CAUTION: I use pthread_cancel, I must take care of the cancel point*/
        pthread_cancel(start->sender);
        start = start->next_station;
    }

    start = g_station_list.head_station;
    while (start != NULL)
    {
        station_t* next = start->next_station;
        
        pthread_mutex_lock(&start->lock);
        clientinfo_t* c = start->connected_clients.head_clients;
        while (c != NULL)
        {
            clientinfo_t* next = c->next_client;
            c->next_client = NULL;
            c->cur_station = NULL;
            c = next;
            start->connected_clients.counter--;
        }
        assert(start->connected_clients.counter == 0);
        /* reset */
        start->connected_clients.head_clients = NULL;
        start->connected_clients.tail_clients = NULL;
        
        for (int i = 0; i < start->song_counter; i++)
            free((void*)start->songs[i]);

        pthread_mutex_unlock(&start->lock);

        pthread_mutex_destroy(&start->lock);
        free(start);
        g_station_list.counter--;
        start = next;
    }
    pthread_mutex_unlock(&g_station_list.lock);
    pthread_mutex_destroy(&g_station_list.lock);
    assert(g_station_list.counter == 0);
    g_station_list.head_station = NULL;
    g_station_list.tail_station = NULL;
}

/*
 init_globals: initialize all of the global variables
 */
void init_globals()
{
    init_clients_info();
    init_stations();
    command_helper();
}

int main(int argc, char* argv[])
{
    const char* tcp_port;
	if (argc < 3)
	{
		usage();
		exit(EXIT_FAILURE);
	}
	else
		tcp_port = argv[1];

    int socket = 0;

    if ((socket = init_listen(tcp_port, NULL)) < 0)
    {
        fprintf(stderr, "error binding socket.\n");
        exit(EXIT_FAILURE);
    }
    
    /* initialize global variables or settings */
    init_globals();

    /* read the stations */
    read_station(&argv[2],argc-2);

    /* create the thread for user input */
    pthread_create(&g_user, NULL, user_input, NULL);
    
    if (server_listen(socket) == -1)
    {
        fprintf(stderr, "error in server.");
        exit(EXIT_FAILURE);
    }

    /* release */
    void*ret = NULL;
    pthread_join(g_user, ret);
    close(socket);
    release_stations();
    pthread_exit(0);

	return 0;
}
