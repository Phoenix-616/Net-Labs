#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

#define BUF_SIZE 4096
#define CONNECT_LIM 500

enum STATUS {IDLE, CONNECTION, WORK};

typedef struct pair
{
	int server_fd;
	int client_fd;
	char * ser_buf;
	char * cli_buf;
	int ser_read;
	int ser_got;
	int cli_read;
	int cli_got;
	enum STATUS stat;
} pair;

pair * pairs;
int activ_connection = 0;

void delete_pair(pair_num)
{
	if ((pairs + pair_num) -> server_fd && ((pairs + pair_num) -> server_fd != -1)) close((pairs + pair_num) -> server_fd);
	if ((pairs + pair_num) -> client_fd && ((pairs + pair_num) -> client_fd != -1)) close((pairs + pair_num) -> client_fd);
	if ((pairs + pair_num) -> ser_buf != NULL) free((pairs + pair_num) -> ser_buf);
	if ((pairs + pair_num) -> cli_buf != NULL) free((pairs + pair_num) -> cli_buf);
	bzero((pairs + pair_num), sizeof(pair));
	activ_connection--;
	fprintf(stderr, "Transfer window closed\n");
}

void recv_data(int * fd, char * buf, int * got, int pair_fd, int pair_num)
{
	int ret = recv(*fd, (buf + *got), (BUF_SIZE - *got), 0);
	if (ret < 0)
	{
		if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
			perror("recv failed");
		return;
	}
	if (ret == 0)
	{
		fprintf(stderr, "%d closed connection\n", *fd);
		if (pair_fd == -1)
		{
			delete_pair(pair_num);
		}
		else
		{
			close(*fd);
			*fd = -1;
		}
	}
	*got += ret;
}

void send_data(int * fd, char * buf, int * rea, int * got, int pair_fd, int pair_num)
{
	int ret = send(*fd, (buf + *rea), (*got - *rea), 0);
	if (ret < 0)
	{
		if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
			perror("send failed\n");
		if (errno == ECONNRESET)
		{
			close(*fd);
			*fd = -1;
			if (pair_fd == -1)
			{
				delete_pair(pair_num);
				return;
			}
		}
	}
	if (ret == 0)
	{
		fprintf(stderr, "Nothing sended to %d\n", *fd);
	}
	*rea += ret;
	if (*rea == *got)
	{
		*rea = 0;
		*got = 0;
		if (pair_fd == -1)
		{
			delete_pair(pair_num);
		}
	}
}

int main(int argc, char *argv[])
{
	int listen_fd;
	struct sockaddr_in server_addr, forward_addr;
	struct hostent * he = NULL;
	int ret;
	fd_set sel_set, sel_set2;
	struct timeval sel_time;
	int ndfs;
	int i;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	
	
	if (argc < 4)
	{
		fprintf(stderr, "Need incoming port, hostname and port of host\n");
		exit(0);
	}
	
	if (!(pairs = (pair *) calloc(sizeof(pair), CONNECT_LIM)))
	{
		perror("calloc() failed");
		exit(0);
	}
	
	
	//SOCKETS CREATION...
	
	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("listen_fd creation problem");
		exit(0);
	}
	//...END
	
	//HOSTNAME RESOLVING
	if ( (he = gethostbyname(argv[2]) ) == NULL )
	{
		herror("hostname resolving failed");
		exit(0);
	}	
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_addr.s_addr = ((struct in_addr *)he->h_addr_list[0]) -> s_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[3]));
	//...END
	
	//FORWARD ADDR CREATION
	bzero(&forward_addr, sizeof(forward_addr));
	forward_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	forward_addr.sin_family = AF_INET;
	forward_addr.sin_port = htons(atoi(argv[1]));
	//...END
	
	if (bind(listen_fd, (struct sockaddr *) &forward_addr, sizeof(forward_addr)))
	{
		perror("forward's socket and addr binding failed");
		exit(0);
	}
	
	if (listen(listen_fd, 10))
	{
		perror("Set listen_fd LISTEN failed");
		exit(0);
	}
	
	fprintf(stderr, "Start...\n");
	while (1)
	{
		ndfs = 0;
		sel_time.tv_sec = 10;
		sel_time.tv_usec = 0;
		
		FD_ZERO(&sel_set);
		FD_ZERO(&sel_set2);
		if (activ_connection < CONNECT_LIM)
		{
			FD_SET(listen_fd, &sel_set);
			ndfs = listen_fd + 1;
		}
		
		//All our nods
		if (activ_connection > 0)
		{
			fprintf(stderr, "Looking for %d connections\n", activ_connection);
			for (i = 0; i < CONNECT_LIM; i++)
			{
				if ((pairs + i) -> stat == CONNECTION)
				{
					FD_SET((pairs[i].server_fd), &sel_set2);
					if (ndfs < (pairs + i) -> server_fd) ndfs = (pairs + i) -> server_fd + 1;
				}
				if ((pairs + i) -> stat == WORK)
				{
					if ((pairs + i) -> server_fd != -1)
					{
						if ((pairs + i) -> cli_got < BUF_SIZE) FD_SET((pairs[i].server_fd), &sel_set);
						if ((pairs + i) -> ser_read < (pairs + i) -> ser_got) FD_SET((pairs[i].server_fd), &sel_set2);
						if (ndfs < (pairs + i) -> server_fd) ndfs = (pairs + i) -> server_fd + 1;
					}
					if ((pairs + i) -> client_fd != -1)
					{
						if ((pairs + i) -> ser_got < BUF_SIZE) FD_SET((pairs[i].client_fd), &sel_set);
						if ((pairs + i) -> cli_read < (pairs + i) -> cli_got) FD_SET((pairs[i].client_fd), &sel_set2);
						if (ndfs < (pairs + i) -> client_fd) ndfs = (pairs + i) -> client_fd + 1;
					}
				}
			}
		}
		
		ret = select(ndfs, &sel_set, &sel_set2, NULL, &sel_time);
		
		if (ret < 0)
		{
			perror("Select crashed");
			continue;
		}
		
		if (ret == 0)
		{
			fprintf(stderr, "Forward idle...\n");
			continue;
		}
		
		if (FD_ISSET(listen_fd, &sel_set))
		{
			do
			{
				//Search for free pair
				for (i = 0; i < CONNECT_LIM; i++)
				{
					if ((pairs + i) -> stat == IDLE) break;
				}
				activ_connection++;
				fprintf(stderr, "Trying accept connection with client\n");
				if (((pairs + i) -> client_fd = accept4(listen_fd, NULL, NULL, O_NONBLOCK)) < 0)
				{
					perror("accept() failed");
					delete_pair(i);
					continue;
				}
			
				//CREATING SERVER SOCKET
				if (((pairs + i) -> server_fd = socket(AF_INET, SOCK_STREAM | O_NONBLOCK, 0)) < 0)
				{
					perror("Creating server socket failed");
					delete_pair(i);
					continue;
				}
				//OPENING CONNECTION...
			
				if (connect((pairs + i) -> server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)))
				{
					if (errno != EINPROGRESS)
					{
						perror("connect() failed");
						delete_pair(i);
						continue;
					}
					(pairs + i) -> stat = CONNECTION;
					fprintf(stderr, "Connection in progress\n");
				}
				else
				{
					(pairs + i) -> stat = WORK;
					fprintf(stderr, "Connection established\n");
				}
			} while (0);
		}
		
		for (i = 0; i < CONNECT_LIM; i++)
		{
			if (((pairs + i) -> stat == CONNECTION) && FD_ISSET((pairs + i) -> server_fd, &sel_set2))
			{
				int result;
				socklen_t result_len = sizeof(result);
				if (getsockopt((pairs + i) -> server_fd, SOL_SOCKET, SO_ERROR, &result, &result_len))
				{
					perror("getsockopt() failed");
					delete_pair(i);
					continue;
				}
				if (result)
				{
					errno = result;
					perror("connection failed");
					delete_pair(i);
					continue;
				}
				(pairs + i) -> stat = WORK;
				if (!((pairs + i) -> ser_buf = (char *) malloc(BUF_SIZE)))
				{
					perror("malloc() failed on server buf");
					delete_pair(i);
					continue;
				}
				if (!((pairs + i) -> cli_buf = (char *) malloc(BUF_SIZE)))
				{
					perror("malloc() failed on client buf");
					delete_pair(i);
					continue;
				}
			}
			
			if (((pairs + i) -> stat == WORK) && FD_ISSET(((pairs + i) -> server_fd), &sel_set))
			{
				recv_data(&((pairs + i) -> server_fd), (pairs + i) -> cli_buf, &((pairs + i) -> cli_got), (pairs + i) -> client_fd, i); 
			}
			if (((pairs + i) -> stat == WORK) && FD_ISSET(((pairs + i) -> client_fd), &sel_set))
			{
				recv_data(&((pairs + i) -> client_fd), (pairs + i) -> ser_buf, &((pairs + i) -> ser_got), (pairs + i) -> server_fd, i);
			}
			if (((pairs + i) -> stat == WORK) && FD_ISSET(((pairs + i) -> server_fd), &sel_set2))
			{
				send_data(&((pairs + i) -> server_fd), (pairs + i) -> ser_buf, &((pairs + i) -> ser_read), &((pairs + i) -> ser_got), (pairs + i) -> client_fd, i);
			}
			if (((pairs + i) -> stat == WORK) && FD_ISSET(((pairs + i) -> client_fd), &sel_set2))
			{
				send_data(&((pairs + i) -> client_fd), (pairs + i) -> cli_buf, &((pairs + i) -> cli_read), &((pairs + i) -> cli_got), (pairs + i) -> server_fd, i);
			}
		}
	}
	
	return 0;
}
