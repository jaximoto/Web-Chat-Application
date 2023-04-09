#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <iostream>
#include <time.h>
#include <iterator>
#include <sys/random.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/timerfd.h>
#include <time.h>


using namespace std;



//#include "hash.h"
#include "duckchat.h"


#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define MAX_MESSAGE_LEN 65536

//typedef map<string,string> channel_type; //<username, ip+port in string>
typedef map<string,struct sockaddr_in> channel_type; //<username, sockaddr_in of user>

typedef map<string, struct sockaddr_in> server_channels; // ip:port, sockaddr_in of server

typedef map<string, int> has_subscribed; // ip:port, int whether server has sent join message to native server
// 0 = false, 1 = true

int s; //socket for listening
struct sockaddr_in server;

map <string, has_subscribed> check_map; // channel, map whether servers have subscribed to this channel
map<string,struct sockaddr_in> usernames; //<username, sockaddr_in of user>
map<string,int> active_usernames; //0-inactive , 1-active
//map<struct sockaddr_in,string> rev_usernames;
map<string,string> rev_usernames; //<ip+port in string, username>
map<string,channel_type> channels;
// need to be able to send join to all adjacent servers for every channel, yes
// need seperate map of channels to other map of servers
map<string,server_channels> s2s_channels; // <channel in string, map<ip:port, sockaddr_in of server> >
//Map to for all adjacent servers
map<string, sockaddr_in> adjacent_servers; //<ip:port in string, sockaddr_in of server>
// Map for channels server is subscribed in:
map<string, int> subscribed_channels; //<channel ins  string, int 1 = active otherwise just remove key val pair

//UniqueID map <ID : int>
map<long, int> ID_map;


void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
// handle_s2s_join message
// Add server broadcast functionality to handle_join message
void handle_s2s_join_message(void *data, struct sockaddr_in sock);
void send_s2s_join_message(char *channel, struct sockaddr_in sock);
void handle_s2s_say_message(void *data, struct sockaddr_in sock);
void send_s2s_say_message(char *channel, char *username, char *text, long ID, struct sockaddr_in sock);
void handle_s2s_leave_message(void *data, struct sockaddr_in sock);
void send_s2s_leave_message(char *channel, struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);

// Init global vars for debug messages
char server_hostname[HOSTNAME_MAX];
char server_port[12];


int main(int argc, char *argv[])
{
	// Must be given odd number of args bc there should be even amount of additional args
	// I.E. 3 + 2 = 5
	if (argc < 3 || argc % 2 == 0)
	{
		printf("Usage: ./server domain_name port_num [neighbor_domain_name1 ... neighbor_port_num1 ...]\n");
		exit(1);
	}

	// TODO MAKe global ip from resolved hostname not localhost
	char hostname[HOSTNAME_MAX];
	int port;
	
	strcpy(hostname, argv[1]);
	port = atoi(argv[2]);
	
	// New!! : set global str vars
	//strcpy(server_hostname, hostname);
	strcpy(server_port, argv[2]);
	for (int n = 3; n < argc; n += 2){
		// process additional args
		// make key string
		int port_num = atoi(argv[n + 1]);
		//string ip(argv[n]);
		string port(argv[n + 1]);
		//string key = ip + "." +port;
		//cout << ip << " " << port << endl;

		//construct sockaddrin of server
		struct sockaddr_in neighbor_sock;
		neighbor_sock.sin_family = AF_INET;
		neighbor_sock.sin_port = htons(port_num);

		struct hostent *h;
		if ((h = gethostbyname(argv[n])) == NULL)
		{
			printf("Error resolving hostname for: %s", argv[n]);
			return EXIT_FAILURE;
		}
		memcpy(&neighbor_sock.sin_addr, h->h_addr_list[0], h->h_length);
		// now make key using resolved address
		string ip = inet_ntoa(neighbor_sock.sin_addr);
		string key = ip + ":" + port;
		//cout << "Neighbor key: " << key << endl;
		//add to server map
		adjacent_servers[key] = neighbor_sock;

	}
		// iterate over map to print
		/*
		map <string, sockaddr_in>::iterator it = adjacent_servers.begin();
		while (it != adjacent_servers.end()){
			cout << it->first << endl;
			printf("%d\n",it->second.sin_port);
			it++;
		}
		*/
	
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror ("socket() failed\n");
		exit(1);
	}

	//struct sockaddr_in server;

	struct hostent     *he;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((he = gethostbyname(hostname)) == NULL) {
		puts("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
	// now init hostname global var
	strcpy(server_hostname,  inet_ntoa(server.sin_addr));
	int err;

	err = bind(s, (struct sockaddr*)&server, sizeof server);

	if (err < 0)
	{
		perror("bind failed\n");
	}
	else
	{
		//printf("bound socket\n");
	}


	


	//testing maps end

	//create default channel Common
	string default_channel = "Common";
	map<string,struct sockaddr_in> default_channel_users;
	channels[default_channel] = default_channel_users;

	

	// create 2 timerfd's
	// one for 1 minute and another for 2 minutes
	int send_state, check_state;
	send_state = timerfd_create(CLOCK_REALTIME, 0);
	check_state = timerfd_create(CLOCK_REALTIME, 0);
	if (send_state == -1 || check_state == -1)
	{
		fprintf(stderr, "Timer file descriptors cannot be created\n");
		return EXIT_FAILURE;
	}

	// Set up itimerspec structures
	struct timespec now;
	struct itimerspec check_value, send_value;

	if (clock_gettime(CLOCK_REALTIME, &now) == -1)
	{
		fprintf(stderr, "Failed getting time\n");
		return EXIT_FAILURE;
	}

	check_value.it_value.tv_sec = now.tv_sec + 120;
	check_value.it_value.tv_nsec = now.tv_nsec;
	check_value.it_interval.tv_sec = 120;
	check_value.it_interval.tv_nsec = 0;

	send_value.it_value.tv_sec = now.tv_sec + 60;
	send_value.it_value.tv_nsec = now.tv_nsec;
	send_value.it_interval.tv_sec = 60;
	send_value.it_interval.tv_nsec = 0;


	// start timers:
	if (timerfd_settime(check_state, TFD_TIMER_ABSTIME, &check_value, NULL) == -1)
	{
		fprintf(stderr, "Could not arm check timer\n");
		printf("fd is: %d\n", check_state);
		return EXIT_FAILURE;
	}

	if (timerfd_settime(send_state, TFD_TIMER_ABSTIME, &send_value, NULL) == -1)
	{
		fprintf(stderr, "Could not arm send timer\n");
		printf("fd is: %d\n", send_state);
		return EXIT_FAILURE;
	}
	


	while(1) //server runs for ever
	{

		//use a file descriptor with a timer to handle timeouts
		int rc;
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(s, &fds);
		FD_SET(send_state, &fds);
		FD_SET(check_state, &fds);
		
		


		rc = select(check_state+1, &fds, NULL, NULL, NULL);
		

		
		if (rc < 0)
		{
			printf("error in select\n");
            getchar();
		}
		else
		{
			int socket_data = 0;

			if (FD_ISSET(s,&fds))
			{
               
				//reading from socket
				handle_socket_input();
				socket_data = 1;

			}
			
			if (FD_ISSET(check_state, &fds))
			{
				//printf("timer went off\n");

				// check for join message responses for every channel from every server
				// do that by having map of servers who have sent join
				// map <channel, below map>
				// map <key, bool> key is ip:port of server, bool is whether they have sent a join or not
				// if true, change bool to false
				// if false send leave message to 
				map <string, has_subscribed> :: iterator channel_check;
				map <string, int> :: iterator server_check;
				map <string, sockaddr_in> :: iterator server_it;
				for (channel_check = check_map.begin(); channel_check != check_map.end(); channel_check++)
				{
					for (server_check = channel_check->second.begin(); server_check != channel_check->second.end(); server_check++)
					{
						if (server_check->second == 1)
						{
							// reset entry to 0
							check_map[channel_check->first][server_check->first] = 0;
							//cout << server_hostname << ":" << server_port << " " << server_check->first << " made check" << endl;
						}
						else
						{
							// server has not responded erase from records
							server_it = s2s_channels[channel_check->first].find(server_check->first);
							if (server_it != s2s_channels[channel_check->first].end())
							{
								//server found and erase from records
								s2s_channels[channel_check->first].erase(server_it);
							}
							else {
								// server found in check_map but not in s2s_map
								// set entry in checkmap to 0
								check_map[channel_check->first][server_check->first] = 0;
							}
							//cout << server_hostname << ":" << server_port << " " << server_check->first << " did not make check" << endl;

						}
					}
				}
				if (clock_gettime(CLOCK_REALTIME, &now) == -1)
					{
						fprintf(stderr, "Failed getting time\n");
						return EXIT_FAILURE;
					}

				check_value.it_value.tv_sec = now.tv_sec + 120;
				check_value.it_value.tv_nsec = now.tv_nsec;
				check_value.it_interval.tv_sec = 120;
				check_value.it_interval.tv_nsec = 0;

				


				// start timers:
				if (timerfd_settime(check_state, TFD_TIMER_ABSTIME, &check_value, NULL) == -1)
				{
					fprintf(stderr, "Could not arm check timer\n");
					printf("fd is: %d\n", check_state);
					return EXIT_FAILURE;
				}

				if (timerfd_settime(send_state, TFD_TIMER_ABSTIME, &send_value, NULL) == -1)
				{
					fprintf(stderr, "Could not arm check timer\n");
					printf("fd is: %d\n", send_state);
					return EXIT_FAILURE;
				}

			}

			if (FD_ISSET(send_state, &fds))
			{
				// Check to see if this works after leave messages and whatnot
				// send a join message for all known channels to all known servers
				//printf("sent timer went off\n");
				// make iterators for channels and servers
				map <string, int> :: iterator send_channels;
				map <string, sockaddr_in> :: iterator send_servers;
				map <string, int> :: iterator check;
				for (send_channels = subscribed_channels.begin(); send_channels != subscribed_channels.end(); send_channels++)
				{
					const char *str = send_channels->first.c_str();
					for (send_servers = adjacent_servers.begin(); send_servers != adjacent_servers.end(); send_servers++)
					{
						// send a join message for channel
						char channel_field[CHANNEL_MAX];
						strcpy(channel_field, str);
						send_s2s_join_message(channel_field, send_servers->second);
						//cout << server_hostname << ":" << server_port << " " << send_servers->first << " Sent refresh join" << endl;
						// update check_map and s2s_channels
						s2s_channels[send_channels->first][send_servers->first] = send_servers->second;

						// see if server in check for channel:
						check = check_map[send_channels->first].find(send_servers->first);
						if (check == check_map[send_channels->first].end())
						{
							// not found make entry
							check_map[send_channels->first][send_servers->first] = 0;

						}
						// else do nothing
						
					}
				}
				 // reset timer
				if (clock_gettime(CLOCK_REALTIME, &now) == -1)
					{
						fprintf(stderr, "Failed getting time\n");
						return EXIT_FAILURE;
					}


				send_value.it_value.tv_sec = now.tv_sec + 60;
				send_value.it_value.tv_nsec = now.tv_nsec;
				send_value.it_interval.tv_sec = 60;
				send_value.it_interval.tv_nsec = 0;


				// start timers:
				if (timerfd_settime(send_state, TFD_TIMER_ABSTIME, &send_value, NULL) == -1)
				{
					fprintf(stderr, "Could not arm check timer\n");
					printf("fd is: %d\n", send_state);
					return EXIT_FAILURE;
				}

			}

			

			

			


		}

		
	}



	return 0;

}
//----------------------------------------Handle_Socket_Input-----------------------------------------
void handle_socket_input()
{

	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof recv_text;


	bytes = recvfrom(s, data, len, 0, (struct sockaddr*)&recv_client, &fromlen);


	if (bytes < 0)
	{
		perror ("recvfrom failed\n");
	}
	else
	{
		//printf("received message\n");

		struct request* request_msg;
		request_msg = (struct request*)data;

		//printf("Message type:");
		request_t message_type = request_msg->req_type;

		//printf("%d\n", message_type);

		if (message_type == REQ_LOGIN)
		{
			handle_login_message(data, recv_client); //some methods would need recv_client
		}
		else if (message_type == REQ_LOGOUT)
		{
			handle_logout_message(recv_client);
		}
		else if (message_type == REQ_JOIN)
		{
			handle_join_message(data, recv_client);
		}
		else if (message_type == REQ_S2S_JOIN)
		{
			handle_s2s_join_message(data, recv_client);
		}
		else if (message_type == REQ_S2S_SAY)
		{
			handle_s2s_say_message(data, recv_client);
		}
		else if (message_type == REQ_S2S_LEAVE)
		{
			handle_s2s_leave_message(data, recv_client);
		}
		else if (message_type == REQ_LEAVE)
		{
			handle_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SAY)
		{
			handle_say_message(data, recv_client);
		}
		else if (message_type == REQ_LIST)
		{
			handle_list_message(recv_client);
		}
		else if (message_type == REQ_WHO)
		{
			handle_who_message(data, recv_client);
		}
		
		else
		{
			//send error message to client
			send_error_message(recv_client, "*Unknown command");
		}




	}


}
//-----------------------------------------------Login--------------------------------------------------------
void handle_login_message(void *data, struct sockaddr_in sock)
{
	struct request_login* msg;
	msg = (struct request_login*)data;

	string username = msg->req_username;
	usernames[username]	= sock;
	active_usernames[username] = 1;

	//rev_usernames[sock] = username;

	//char *inet_ntoa(struct in_addr in);
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;
	//unsigned short short_port = sock.sin_port;
	//cout << "short port: " << short_port << endl;
	//cout << "port: " << port << endl;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;
	rev_usernames[key] = username;
	cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request login " << username << endl;
	//cout << "server: " << username << " logs in" << endl;

	



}

// -----------------------------------------Logout----------------------------------------------------
void handle_logout_message(struct sockaddr_in sock)
{

	//construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;

	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;

	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/




	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		//cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);

		//remove from usernames
		map<string,struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		//remove from all the channels if found
		map<string,channel_type>::iterator channel_iter;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			//cout << "key: " << iter->first << " username: " << iter->second << endl;
			//channel_type current_channel = channel_iter->second;
			map<string,struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.find(username);
			if (within_channel_iterator != channel_iter->second.end())
			{
				channel_iter->second.erase(within_channel_iterator);
			}

		}


		//remove entry from active usernames also
		//active_usernames[username] = 1;
		map<string,int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);

		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request logout " << username << endl;
		//cout << "server: " << username << " logs out" << endl;
	}


	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/


	//if so delete it and delete username from usernames
	//if not send an error message - later

}
//_______________________________________________SEND_S2S_Join____________________________________
void send_s2s_join_message(char *channel, struct sockaddr_in sock)
{
	// Nah don't do that only handle bookkeeping outside func
	// need to do seperate bookkeeping on both handlers anyways

	// build a s2s join message and send it to appropriate socket
	// 
	size_t len;
	//map <string, sockaddr_in>::iterator it = adjacent_servers.begin();
	//while (it != adjacent_servers.end())
	
	// Make s2S_join request:
	void *data;
	struct request_S2S_join join_message;
	join_message.req_type = REQ_S2S_JOIN;
	strcpy(join_message.req_channel, channel);
	data = &join_message;
	ssize_t e;
	struct sockaddr_in neighbor = sock;
	len = sizeof join_message;

	// Make ip and port string for neighbor
	string ip = inet_ntoa(neighbor.sin_addr);

	int port = ntohs(neighbor.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	
	// Send message
	e = sendto(s, data, len, 0, (struct sockaddr*)&neighbor, sizeof neighbor);
	if (e < 0)
	{
		perror("S2S join failed");
	}
	else
	{
		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send S2S Join " << channel << endl;

	}
	
}

//__________________________________________Handle_S2S_Join____________________________________________
void handle_s2s_join_message(void *data, struct sockaddr_in sock)
{
	
	//when sending and recieving messages
	
	// Ports may be in network order
	// Host string should use resolved ip not localhost strs
	// maybe set that when setting constant vals and using command line args rather than resolving hostnames individually.






	// check if channel is already made in records:
	// if yes, do nothing
	// if no, create channel, and send join message to all other adjacent servers
	//other than the one recieved

	//get message fields
	struct request_S2S_join*msg;
	msg = (struct request_S2S_join *)data;

	string channel = msg->req_channel;

	// Save char * representation to send later
	char send_channel[CHANNEL_MAX];
	strcpy(send_channel, msg->req_channel);

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	//  ADD Debug STRING
	cout << server_hostname << ":" << server_port << " " << ip << ":" << port_str << " recv S2S Join " << channel << endl;
	//Check if key/sender is in adjacent servers if yes proceed, print error
	map<string, sockaddr_in> :: iterator iter;
	iter = adjacent_servers.find(key);
	if (iter == adjacent_servers.end() && adjacent_servers.size() > 1)
	{
		cout << server_hostname << ":" << server_port << " " << ip << ":" << port_str << " Server cannot be found" << endl;
		return;
	}
	else
	{
		// check if channel is subscribed by server
		// if yes check to see if sending server is in s2s_channels[channel] and add it, if no send a join request to all neighbors
		map<string, int> :: iterator sub;
		sub = subscribed_channels.find(channel);
		if (sub == subscribed_channels.end())
		{
			map<string, server_channels> :: iterator outer;
			outer = s2s_channels.find(channel); //should equal s2s_channels end bc it is not a subbed channel
			// if this channel is found then there is a bug:
			if (outer != s2s_channels.end())
			{
				printf("subscribed_channels can't find channel, but s2s_channels can\n");
				//printf("size of map is: %ld\n", s2s_channels.size());
				return;
			}
			else
			{
				// subscribe to channel:
				subscribed_channels[channel] = 1;
				// add entry of sender, and then forward message to every other server
				//outer->second[key] = sock;

				
				// sender HAS sent a join message for server
				// loop through adjacent servers; add entry of each to s2s channel for respective channel
				// send s2s_join to all servers except caller
				// *key is key of caller*
				map<string, sockaddr_in> :: iterator inner;
				for(inner = adjacent_servers.begin(); inner != adjacent_servers.end(); inner++)
				{
					if (inner->first == key)
					{
						
						s2s_channels[channel][inner->first] = inner->second;
						// add entry of sender to subscribed servers for each channel
						check_map[channel][key] = 1;
						//printf("added entry to check_map \n");
						// add entry of channel to user map
						map <string, struct sockaddr_in> users;
						channels[channel] = users;
					}
					else
					{
					// Add sent server to list of subscribed servers
					// send join message to all adjacent servers other than prev sender:
					//cout << "key is: " << key << " iterator is: " << inner->first << endl;
					send_s2s_join_message(send_channel, inner->second);
					// outer->second = map in s2s_channels
					//iter->first = key of entry in adjacent servers
					// iter->second = value of entry in adjacent servers
					//s2s_servers[channel][adj_key] = adj_value
					s2s_channels[channel][inner->first] = inner->second;

					// add entry of other server for the channel check
					//check_map[channel][inner->first] = 0;
					// 0 bc they haven't sent a join yet

					// add entry of channel to user map
					//map <string, struct sockaddr_in> users;
					//channels[channel] = users;
					}
					
				}
				//debug print out newly made map:
				/*
				map<string, server_channels> :: iterator outer;
				map<string, struct sockaddr_in> :: iterator value;
				cout << server_hostname << ":" << server_port << " map:" << endl;
				for (outer = s2s_channels.begin(); outer != s2s_channels.end(); outer++)
				{
					cout << outer->first + ":" << endl;
					for (value = outer->second.begin(); value != outer->second.end(); value++)
					{
						cout << "	" << value->first << endl;
					}
				}
				// Need to also add new channel to channel map for users
				map<string,struct sockaddr_in> new_channel_users;
				channels[channel] = new_channel_users;
				*/
			}

			
		}
		else
		{
			// server was found in subcribed channels
			// meaning native server knows channel
			// now need to make sure that sender is in native server's records cooresponding to channel
			// if not add it and end handler
			// otherwise ignore message because there is nothing to be done
			map<string, server_channels> :: iterator outer;
			outer = s2s_channels.find(channel); 
			if (outer == s2s_channels.end())
			{
				// error if subscribed should be in channel map
				printf("In handle s2s join: server subscribed to channel but not in server map\n");
				//printf("size of map is: %ld\n", s2s_channels.size());
				return;
			}
			else
			{
				map<string, sockaddr_in> :: iterator server_in_map;
				server_in_map = s2s_channels[channel].find(key);
				if (server_in_map == s2s_channels[channel].end())
				{
					// server not found in records, add it:
					s2s_channels[channel][key] = sock;
					
				}
				// otherwise nothing to be done so return
				// actually mark off checkmap that this server has sent the native server a join message regardless of if statement
				check_map[channel][key] = 1;
				return;
			}
			
		}
	}
}
//-------------------------------------------Join-------------------------------------------------------
void handle_join_message(void *data, struct sockaddr_in sock)
{
	//TODO MAKE SURE THIS CREATES CHANNEL KEY IN USER CHANNEL MAP TOO!
	//get message fields
	struct request_join* msg;
	msg = (struct request_join*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;
		// New: if channel found or not print recieve debug message from client
		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request join " << username + " " << channel << endl;
		//check if server is subscribed to channel:
		map<string, int> :: iterator iter;
		iter = subscribed_channels.find(channel);

		if (iter == subscribed_channels.end())
		{
			// server not subscribed
			// NEW add channel to subscribed channel map for server:
			subscribed_channels[channel] = 1;
			
			// might need to add my own server to internal record of s2s_channels
			
			// turn string back into char array for function:
			char channel_msg[CHANNEL_MAX];
			strcpy(channel_msg, channel.c_str());
			// loop through and send to all servers coorelating to channel
			map<string, struct sockaddr_in> :: iterator inner;
			for (inner = adjacent_servers.begin(); inner != adjacent_servers.end(); inner++)
			{
				//string sent_ip = inet_ntoa(inner->second.sin_addr);

				//int sent_port = inner->second.sin_port;

				//char sent_port_str[6];
				//sprintf(sent_port_str, "%d", sent_port);
				// send message to server
				send_s2s_join_message(channel_msg, inner->second);
				// send message in function
				//cout << server_hostname << ":" << server_port << " " << inner->first << " send S2S Join " << channel << endl;

				// add it to s2s map subscribed to channel
				s2s_channels[channel][inner->first] = inner->second;

				// add server entry for join check:
				check_map[channel][inner->first] = 0;
			}
			//debug print out newly made map:
			/*
			map<string, server_channels> :: iterator outer;
			map<string, struct sockaddr_in> :: iterator value;
			cout << server_hostname << ":" << server_port << " map:" << endl;
			for (outer = s2s_channels.begin(); outer != s2s_channels.end(); outer++)
			{
				cout << outer->first + ":" << endl;
				for (value = outer->second.begin(); value != outer->second.end(); value++)
				{
					cout << "	" << value->first << endl;
				}
			}
			*/
		}

		if (channel_iter == channels.end())
		{
			//channel not found
			map<string,struct sockaddr_in> new_channel_users;
			new_channel_users[username] = sock;
			channels[channel] = new_channel_users;
			//cout << "creating new channel and joining" << endl;

			

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in>* existing_channel_users;
			//existing_channel_users = &channels[channel];
			//*existing_channel_users[username] = sock;

			channels[channel][username] = sock;
			//cout << "joining exisitng channel" << endl;


		}
		//cout << "server: " << username << " joins channel " << channel << endl;


	}

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if channel is there add user to the channel
	//if channel is not there add channel and add user to the channel


	// call s2s_join(channel)


}
// ____________________________________________S2S_Leave__________________________________________________
void handle_s2s_leave_message(void *data, struct sockaddr_in sock)
{
	//when sending and recieving messages
	
	// Ports may be in network order
	// Host string should use resolved ip not localhost strs
	// maybe set that when setting constant vals and using command line args rather than resolving hostnames individually.
	// check if channel is already made in records:
	// if yes, do nothing
	// if no, create channel, and send join message to all other adjacent servers
	//other than the one recieved

	//get message fields
	struct request_S2S_leave*msg;
	msg = (struct request_S2S_leave *)data;

	string channel = msg->req_channel;

	// Save char * representation to send later
	char send_channel[CHANNEL_MAX];
	strcpy(send_channel, msg->req_channel);

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	//  ADD Debug STRING
	cout << server_hostname << ":" << server_port << " " << ip << ":" << port_str << " recv S2S Leave " << channel << endl;
	//Check if key/sender is in adjacent servers if yes proceed, print error
	map<string, sockaddr_in> :: iterator iter;
	iter = adjacent_servers.find(key);
	if (iter == adjacent_servers.end())
	{
		cout << server_hostname << ":" << server_port << " " << ip << ":" << port_str << " Server cannot be found" << endl;
		return;
	}
	else
	{
		// check if channel is subscribed by server
		// if yes do erase sending server from records for channel, if no error network error
		map<string, int> :: iterator sub;
		sub = subscribed_channels.find(channel);
		if (sub != subscribed_channels.end())
		{
			map<string, server_channels> :: iterator outer;
			outer = s2s_channels.find(channel); //should equal s2s_channels end bc it is not a subbed channel
			
			if (outer == s2s_channels.end())
			{
				printf("subscribed_channels can find channel, but s2s_channels can't\n");
				//printf("size of map is: %ld\n", s2s_channels.size());
				return;
			}
			else
			{
				// Remove entry of sender
				// only prune trees after say message
				// if recving server has no users or servers to forward next say message
				// after erasing the sending server that will be caught in next say message
				iter = s2s_channels[channel].find(key);
				if (iter == s2s_channels[channel].end())
				{
					printf("Error cannot find sending server in internal records of channel\n");
					return;
				}
				// found server in respective channel now remove from inner map
				s2s_channels[channel].erase(iter);
				// now we should be done, everything else is done by sender of leave request.
				// need to get rid of
				map<string, int> :: iterator check;
				check = check_map[channel].find(key);
				if (check == check_map[channel].end())
				{
					// never added to check_map so return
					return;
				}
				check_map[channel].erase(check);
				
			}
			//debug print out newly made map:
			/*
			map<string, server_channels> :: iterator p_outer;
			map<string, struct sockaddr_in> :: iterator value;
			cout << server_hostname << ":" << server_port << " map:" << endl;
			for (p_outer = s2s_channels.begin(); p_outer != s2s_channels.end(); p_outer++)
			{
				cout << p_outer->first + ":" << endl;
				for (value = p_outer->second.begin(); value != p_outer->second.end(); value++)
				{
					cout << "	" << value->first << endl;
				}
			}*/

			
		}
		else
		{
			
			return; 
		}
	}
}
//_______________________________________________SEND_S2S_Leave____________________________________
void send_s2s_leave_message(char *channel, struct sockaddr_in sock)
{
	//build leave message and send it to sock
	size_t len;
	//map <string, sockaddr_in>::iterator it = adjacent_servers.begin();
	//while (it != adjacent_servers.end())
	
	// Make s2S_join request:
	void *data;
	struct request_S2S_leave leave_message;
	leave_message.req_type = REQ_S2S_LEAVE;
	strcpy(leave_message.req_channel, channel);
	data = &leave_message;
	ssize_t e;
	struct sockaddr_in neighbor = sock;
	len = sizeof leave_message;

	// Make ip and port string for neighbor
	string ip = inet_ntoa(neighbor.sin_addr);

	int port = ntohs(neighbor.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	
	// Send message
	e = sendto(s, data, len, 0, (struct sockaddr*)&neighbor, sizeof neighbor);
	if (e < 0)
	{
		perror("S2S leave failed");
	}
	else
	{
		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send S2S Leave " << channel << endl;

	}
	
}
//------------------------------------------------Leave--------------------------------------------
void handle_leave_message(void *data, struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes, remove user from channel
	//if not send an error message to the user


	//get message fields
	struct request_leave* msg;
	msg = (struct request_leave*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request leave " << username + " "<< channel << endl;

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to leave non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to leave channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].erase(channel_user_iter);
				//existing_channel_users.erase(channel_user_iter);
				cout << "server: " << username << " leaves channel " << channel <<endl;

				//delete channel if no more users
				if (channels[channel].empty() && (channel != "Common"))
				{
					channels.erase(channel_iter);
					cout << "server: " << "removing empty channel " << channel <<endl;
				}

			}


		}




	}



}



// ______________________________________________________HANDLE SERVER SAY_______________________________________________________________
void handle_s2s_say_message(void *data, struct sockaddr_in sock)
{
	// Check if unique message ID is in map, if yes, send leave message, if no add it to map
	// first send message to all clients in channel
	// then send message to all servers on channel

	// bools for reciever to send leave message to sender
	bool is_users = true;
	bool is_servers = true;
	//get message fields
	struct request_S2S_say *msg;
	msg = (struct request_S2S_say *) data;
	
	string channel = msg->req_channel;
	char send_channel[CHANNEL_MAX];
	strcpy(send_channel,msg->req_channel);
	string text = msg->req_text;

	string ip = inet_ntoa(sock.sin_addr);
	int port_num = ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port_num);

	

	string username = msg->req_username;

	long ID = msg->req_id;

	map<long, int> :: iterator unique;
	unique = ID_map.find(ID);
	if (unique != ID_map.end())
	{
		//found duplicate, send leave message to sender
		//cout << server_hostname << " " << server_port << " Found a duplicate say message" << endl;
		send_s2s_leave_message(send_channel, sock);
		return;
	}
	else
	{
		//add to map,
		// process rest of request
		ID_map[ID] = 1;

		// check if server in adjacent server list:
		// Make key
		string key = ip + ":" + port_str;

		//Make iterator and check if in adjacent_servers
		map<string, struct sockaddr_in> :: iterator found_server;
		found_server = adjacent_servers.find(key);
		if (found_server == adjacent_servers.end())
		{
			cout << server_hostname << ":" << server_port << " Error: S2S Say recv from unkown server" << endl;
			return;
		}
		
		// Server is found, continue process:
		cout << server_hostname << ":" << server_port << " " << key << " recv S2S say " << username + " "<< channel + " " << "\"" + text + "\"" << endl;

		// check if native server subscribed and check if this channel in s2s channels perhaps
		map<string, int> :: iterator sub_channel_iter;
		sub_channel_iter = subscribed_channels.find(channel);
		if (sub_channel_iter == subscribed_channels.end())
		{
			cout << "Error channel not subscribed by: " << server_hostname << ":" << server_port << endl;
			return;
			// also might check if s2s_channel map has channel as well
		}
		//Look for any users subscribed to channel and send to them:
		// Then look for any servers subscribed to channels

		map<string, channel_type> :: iterator channel_iter;
		channel_iter = channels.find(channel);
		if (channel_iter == channels.end())
		{
			//channel not found
			// means no users
			is_users = false;
			//cout << "Error: channel does not exist for " << server_hostname << ":" << server_port << endl;

		} 
		else
		{
			// channel found: now find users
			if (channels[channel].size() > 0)
			{
				map<string, struct sockaddr_in> :: iterator channel_usr_iter;
				for(channel_usr_iter = channels[channel].begin(); channel_usr_iter != channels[channel].end(); channel_usr_iter++)
				{
					ssize_t bytes;
					void *send_data;
					size_t len;

					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.txt_channel, str);
					str = username.c_str();
					strcpy(send_msg.txt_username, str);
					str = text.c_str();
					strcpy(send_msg.txt_text, str);
					//send_msg.txt_username, *username.c_str();
					//send_msg.txt_text,*text.c_str();
					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in send_sock = channel_usr_iter->second;
					// get ip and port for printing purposes:
					string send_ip = inet_ntoa(send_sock.sin_addr);
					int  send_port =send_sock.sin_port;
					char send_port_str[6];
					sprintf(send_port_str, "%d", send_port);


					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
					else
					{
						cout << server_hostname << ":" << server_port << " " << send_ip << ":" << send_port_str << " send Request say " << username + " " << channel + " " << "\"" << str << "\"" << endl;

						//printf("Message sent\n");

					}

				}

			}
			else
			{
				// set bool that ther are no users in channel
				is_users = false;
			}
		}
		// check if there are more than just sending server
		map <string, server_channels> :: iterator channel_finder;
		channel_finder = s2s_channels.find(channel);
		if (channel_finder == s2s_channels.end())
		{
			// no channels found on servers
			cout << "say found no channel for s2s_channel of channels" << endl;
			return;
		}
		if (s2s_channels[channel].size() == 1)
		{
			is_servers = false;
		}
		if (is_users == false && is_servers == false)
		{
			// send leave message to sending server
			// then remove server from records
			// Records = subscribed_server map
			// remove sock at s2s_channels[channel][sock ip and port]
			// remove channel from s2s_channels
			// remove channel from channels
			//printf("sent leave message \n");
			send_s2s_leave_message(send_channel, sock);
			if (key == "Common")
			{
				map <string, sockaddr_in> :: iterator channel_server;
				channel_server = s2s_channels[channel].find(key);
				s2s_channels[channel].erase(channel_server);
				return;
			}
			else
			{
				map <string, int> :: iterator subbed;
				subbed = subscribed_channels.find(channel);
				subscribed_channels.erase(subbed);

				map <string, sockaddr_in> :: iterator channel_server;
				channel_server = s2s_channels[channel].find(key);
				s2s_channels[channel].erase(channel_server);

				map <string, server_channels> :: iterator channel_key;
				channel_key = s2s_channels.find(channel);
				s2s_channels.erase(channel_key);
				return;
			}
			
		}
		//printf(" made past if statement\n");
		//printf("is_user: %d is_server: %d\n", is_users, is_servers);
		// if No users in channel do nothing
		// check
		// still needs to send s2s message
		// loop through server channels map and send s2s messages to each server interested
		
		// prep send msg fields:
		const char *str;
		str = channel.c_str();
		char send_channel[CHANNEL_MAX];
		char send_user[USERNAME_MAX];
		char send_text[SAY_MAX];
		strcpy(send_channel, str);
		str = username.c_str();
		strcpy(send_user, str);
		str = text.c_str();
		strcpy(send_text, str);
		map<string, struct sockaddr_in> :: iterator subbed_iter;
		for (subbed_iter = s2s_channels[channel].begin(); subbed_iter != s2s_channels[channel].end(); subbed_iter++)
		{
			if (subbed_iter->second.sin_addr.s_addr == sock.sin_addr.s_addr && subbed_iter->second.sin_port == sock.sin_port)
			{
				// Don't send say message back to sender
				continue;
			}
			// send s2s say message to all servers
			string sendIP = inet_ntoa(subbed_iter->second.sin_addr);
			int send_port_num = ntohs(subbed_iter->second.sin_port);
			char send_port_str[6];
			sprintf(send_port_str, "%d", send_port_num);

			send_s2s_say_message(send_channel, send_user, send_text, ID, subbed_iter->second);
			cout << server_hostname << ":" << server_port << " " << sendIP << ":" << send_port_str << " send S2S say " << username + " "<< channel + " " << "\"" << text << "\"" << endl;
		}
			
		
			
		
		
	}
	

}

//__________________________________________SEND SERVER SAY_______________________________________
void send_s2s_say_message(char *channel, char *username,char *text, long ID, struct sockaddr_in sock)
{
	// make message fields
	ssize_t bytes;
	size_t len;
	void *send_data;
	struct request_S2S_say say_msg;
	say_msg.req_type = REQ_S2S_SAY;
	say_msg.req_id = ID;
	strcpy(say_msg.req_channel, channel);
	strcpy(say_msg.req_username, username);
	strcpy(say_msg.req_text, text);
	send_data = &say_msg;

	len = sizeof say_msg;

	bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);

	if (bytes < 0)
	{
		perror("Say message failed to send");
		
	}
	


}

void handle_say_message(void *data, struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes send the message to all the members of the channel
	//if not send an error message to the user


	//get message fields
	struct request_say* msg;
	msg = (struct request_say*)data;

	string channel = msg->req_channel;
	string text = msg->req_text;


	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to send a message to non-existent channel " << channel << endl;

		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to send a message to channel " << channel  << " where he/she is not a member" << endl;
			}
			else
			{
				cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request say " << username + " " << channel  + " "<< "\"" << text << "\"" << endl;

				map<string,struct sockaddr_in> existing_channel_users;
				existing_channel_users = channels[channel];
				for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
				{
					//cout << "key: " << iter->first << " username: " << iter->second << endl;

					ssize_t bytes;
					void *send_data;
					size_t len;

					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;

					const char* str = channel.c_str();
					strcpy(send_msg.txt_channel, str);
					str = username.c_str();
					strcpy(send_msg.txt_username, str);
					str = text.c_str();
					strcpy(send_msg.txt_text, str);
					//send_msg.txt_username, *username.c_str();
					//send_msg.txt_text,*text.c_str();
					send_data = &send_msg;

					len = sizeof send_msg;

					//cout << username <<endl;
					struct sockaddr_in send_sock = channel_user_iter->second;


					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

					if (bytes < 0)
					{
						perror("Message failed\n"); //error
					}
					else
					{
						cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send Request say " << username + " " << channel + " " << "\"" << str << "\"" << endl;

						//printf("Message sent\n");

					}

				}
				//cout << "server: " << username << " sends say message in " << channel <<endl;

			}
			// check if native server subscribed and check if this channel in s2s channels perhaps
			map<string, int> :: iterator sub_channel_iter;
			sub_channel_iter = subscribed_channels.find(channel);
			if (sub_channel_iter == subscribed_channels.end())
			{
				cout << "Error channel not subscribed by: " << server_hostname << ":" << server_port << endl;
				return;
				// also might check if s2s_channel map has channel as well
			}
			else
			{
				// make UNIQUE ID
				size_t bufsize = 64;
				u_int64_t buf;
				//cout << buf << endl;
				char *endptr;
				ssize_t seed = getrandom(&buf, bufsize, GRND_NONBLOCK);
				if (seed != 64)
				{
					perror("getrandom failed");
					return;
				}
				//printf("Seed is: " "%" PRIu64 "\n", buf);
				/*
				errno = 0; // To distinguish success/failure after call
				long seed_num = atol(buf);
				if (errno != 0)
				{
					perror("strtoul");
				}
				if ( endptr == buf)
				{
					fprintf(stderr, "No digits were found\n");
				}
				cout << "Seed_num: " << seed_num << endl;
				*/
				srandom(buf);
				long ID = random();
				//cout << "ID: " << ID << endl;
				// loop through server channels map and send s2s messages to each server interested
				// prep send msg fields:
				const char *str;
				str = channel.c_str();
				char send_channel[CHANNEL_MAX];
				char send_user[USERNAME_MAX];
				char send_text[SAY_MAX];
				strcpy(send_channel, str);
				str = username.c_str();
				strcpy(send_user, str);
				str = text.c_str();
				strcpy(send_text, str);
				map<string, struct sockaddr_in> :: iterator subbed_iter;
				for (subbed_iter = s2s_channels[channel].begin(); subbed_iter != s2s_channels[channel].end(); subbed_iter++)
				{
					
					// send s2s say message to all servers
					string sendIP = inet_ntoa(subbed_iter->second.sin_addr);
					int send_port_num = ntohs(subbed_iter->second.sin_port);
					char send_port_str[6];
					sprintf(send_port_str, "%d", send_port_num);

					send_s2s_say_message(send_channel, send_user, send_text, ID, subbed_iter->second);
					cout << server_hostname << ":" << server_port << " " << sendIP << ":" << send_port_str << " send S2S say " << username + " " << channel + " " << "\"" << text << "\"" << endl;
				}
			}

		}




	}



}


void handle_list_message(struct sockaddr_in sock)
{

	//check whether the user is in usernames
	//if yes, send a list of channels
	//if not send an error message to the user



	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request list " << username + " " << endl;

		int size = channels.size();
		//cout << "size: " << size << endl;

		active_usernames[username] = 1;

		ssize_t bytes;
		void *send_data;
		size_t len;


		//struct text_list temp;
		struct text_list *send_msg = (struct text_list*)malloc(sizeof (struct text_list) + (size * sizeof(struct channel_info)));


		send_msg->txt_type = TXT_LIST;

		send_msg->txt_nchannels = size;


		map<string,channel_type>::iterator channel_iter;



		//struct channel_info current_channels[size];
		//send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;

		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char* str = current_channel.c_str();
			//strcpy(current_channels[pos].ch_channel, str);
			//cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels)+pos)->ch_channel, str);
			//strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			//cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;

			pos++;

		}



		//send_msg.txt_channels =
		//send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof (struct text_list) + (size * sizeof(struct channel_info));

					//cout << username <<endl;
		struct sockaddr_in send_sock = sock;


		//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
		bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
		else
		{
			//printf("Message sent\n");
			cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send Request list " << username + " " << endl;

		}

		cout << "server: " << username << " lists channels"<<endl;


	}



}


void handle_who_message(void *data, struct sockaddr_in sock)
{


	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if yes, send user list in the channel
	//if not send an error message to the user


	//get message fields
	struct request_who* msg;
	msg = (struct request_who*)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;


	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;


	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		active_usernames[username] = 1;

		map<string,channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << "server: " << username << " trying to list users in non-existing channel " << channel << endl;

		}
		else
		{
			cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " recv Request who " << username + " " << channel << endl;

			//channel exits
			map<string,struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel];
			int size = existing_channel_users.size();

			ssize_t bytes;
			void *send_data;
			size_t len;


			//struct text_list temp;
			struct text_who *send_msg = (struct text_who*)malloc(sizeof (struct text_who) + (size * sizeof(struct user_info)));


			send_msg->txt_type = TXT_WHO;

			send_msg->txt_nusernames = size;

			const char* str = channel.c_str();

			strcpy(send_msg->txt_channel, str);



			map<string,struct sockaddr_in>::iterator channel_user_iter;

			int pos = 0;

			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;

				str = username.c_str();

				strcpy(((send_msg->txt_users)+pos)->us_username, str);


				pos++;



			}

			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));

						//cout << username <<endl;
			struct sockaddr_in send_sock = sock;


			//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send Request who " << username + " " << channel << endl;

				//printf("Message sent\n");

			}

			cout << "server: " << username << " lists users in channnel "<< channel << endl;




			}




	}




}



void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	// New get socket info for debug message:
	string ip = inet_ntoa(sock.sin_addr);

	int port = sock.sin_port;

 	char port_str[6];
	sprintf(port_str, "%d", port);
	// May resume with regularly scheduled program...

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char* str = error_msg.c_str();
	strcpy(send_msg.txt_error, str);

	send_data = &send_msg;

	len = sizeof send_msg;


	struct sockaddr_in send_sock = sock;



	bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
	
	if (bytes < 0){
		perror("Message Failed");
	}
	else
	{
		cout << server_hostname << ":" << server_port << " " << ip + ":" + port_str << " send Request error " << endl;

		//printf("Message sent\n");

	}





}






