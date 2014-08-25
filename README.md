% README %
% Snowcast %

1.Introduction:

	Snowcast has three programs to simulate a simple Internet Radio Station, written in C.
	
	snowcast_server handles most of the requests from clients. Server supports multiple
	stations and also modifying the stations. It's responsible for sending the song to the
	listener using datagram and receiving connection from client using TCP/IP protocol.
	
	snowcast_listner is a UDP client responsible for receiving UDP datagram from server.
	It redirects the song data to mpg123 in order to listen the song.
	
	snowcast_client is the client program used for connecting with the server and controling
	which station you want to listen. 
	
2. Execution:

	To run the server: ./snowcast_server [tcp_port] [station path/song path 1]
	To run the client: ./snowcast_control [server_name/server_ip] [tcp_port] [udp_port]
	To run the listner: ./snowcast_listner [udp_port]
	