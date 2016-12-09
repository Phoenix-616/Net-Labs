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
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
	int listen_fd, server_fd, client_fd;
	struct sockaddr_in server_addr, forward_addr;
	struct hostent * he = NULL;
	char cl_buf[BUF_SIZE];
	char ser_buf[BUF_SIZE];
	int cl_read = 0, cl_got = 0;
	int ser_read = 0, ser_got = 0;
	int ret;
	fd_set sel_set, sel_set2;
	struct timeval sel_time;
	int ndfs;
	
	struct addrinfo hints, *servinfo, *p;
	int rv;
	
	char transfer = 0;													//USED AS STATUS OF PROGRAM
	
	
	if (argc < 4)
	{
		fprintf(stderr, "Need incoming port, hostname and port of host\n");
		exit(0);
	}
	
	//SOCKETS CREATION...
	
	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("listen_fd creation problem");
		exit(0);
	}
	
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("server_fd creation problem");
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
	
	//SOCKET TO NON-BLOCK
	 if (0 > (ret = fcntl(server_fd, F_GETFL, 0)))
	 {
		 perror("Getting server_fd's flags failed");
		 exit(0);
	 }
	 ret = ret | O_NONBLOCK;
	 if ((fcntl(server_fd, F_SETFL, ret)) < 0)
	 {
		 perror("Setting server_fd's flags failed");
		 exit(0);
	 }
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
	
	while (1)
	{
		sel_time.tv_sec = 10;
		sel_time.tv_usec = 0;
		
		FD_ZERO(&sel_set);
		FD_ZERO(&sel_set2);
			
		if (transfer)
		{
			FD_SET(server_fd, &sel_set);
			FD_SET(client_fd, &sel_set);
			FD_SET(server_fd, &sel_set2);
			FD_SET(client_fd, &sel_set2);
			ndfs = ((client_fd > server_fd) ? (client_fd) : (server_fd)) + 1;
		}
		else
		{
			fprintf(stderr, "Waiting for connection...\n");
			FD_SET(listen_fd, &sel_set);
			ndfs = listen_fd + 1;
		}
		
		ret = select(ndfs, &sel_set, &sel_set2, NULL, &sel_time);
		if (ret < 0)
		{
			perror("Select crashed");
			exit(0);
		}
		
		if ((ret > 0) && (!transfer))
		{
			fprintf(stderr, "Trying accept connection with client\n");
			if ((client_fd = accept(listen_fd, NULL, NULL)) < 0)			//accept new connection
			{
				perror("accept() failed");
				exit(0);
			}
			//SET NEW SOCKET NON-BLOCKING
			if (0 > (ret = fcntl(client_fd, F_GETFL, 0)))
			{
				perror("Getting client_fd's flags failed");
				exit(0);
			}
			ret = ret | O_NONBLOCK;
			if ((fcntl(client_fd, F_SETFL, ret)) < 0)
			{
				perror("Setting client_fd's flags failed");
				exit(0);
			}
			//OPENING CONNECTION...
			
			if (connect(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)))
			{
				if (errno != EINPROGRESS)
				{
					perror("connect() failed");
					exit(0);
				}
				fprintf(stderr, "Connection in progress\n");
				sel_time.tv_sec = 10;
				sel_time.tv_usec = 0;
				FD_ZERO(&sel_set);
				FD_SET(server_fd, &sel_set);
				ndfs = server_fd + 1;
				if ((ret = select(ndfs, NULL, &sel_set, NULL, &sel_time)) < 0)
				{
					perror("select in CONNECTION failed");
					exit(0);
				}
				if (ret == 0)
				{
					fprintf(stderr, "No connection in timeout\n");
					exit(0);
				}
				if (ret > 0)
				{
					int result;
					socklen_t result_len = sizeof(result);
					if (getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &result, &result_len))
					{
						perror("getsockopt() failed");
						exit(0);
					}
					if (result)
					{
						errno = result;
						perror("connection failed");
						exit(0);
					}
				}
			}
			transfer = 1;
			fprintf(stderr, "Connection established\n");
		}
		/*
		if ((ret > 0) && (transfer))
		{
			if (FD_ISSET(client_fd, &sel_set) && FD_ISSET(server_fd, &sel_set2))
			{
				fprintf(stderr, "DATA FROM CLIENT\n");
				ret = recv(client_fd, buf, BUF_SIZE, 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("recv from client failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "client closed connection\n");
						exit(0);
					}
					fprintf(stderr, "DATA TO SERVER\n");
					ret = send(server_fd, buf, ret, 0);
					if (ret < 0)
					{
						perror("Send to server failed");
					}
				}
			}
			if (FD_ISSET(server_fd, &sel_set) && FD_ISSET(client_fd, &sel_set2))
			{
				fprintf(stderr, "DATA FROM SERVER\n");
				ret = recv(server_fd, buf, BUF_SIZE, 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("recv from server failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "server closed connection\n");
						exit(0);
					}
					if (transfer)
					{
						fprintf(stderr, "DATA TO CLIENT\n");
						ret = send(client_fd, buf, ret, 0);
						if (ret < 0)
						{
							perror("Send to client failed");
						}
					}
				}
			}
		}
		*/
		
		if ((ret > 0) && (transfer))
		{
			if ((FD_ISSET(server_fd, &sel_set)) && (cl_got < BUF_SIZE))
			{
				ret = recv(server_fd, (cl_buf + cl_got), (BUF_SIZE - cl_got), 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("recv from server failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "Server closed connection\n");
						exit(0);
					}
					cl_got += ret;
				}
			}
			if ((FD_ISSET(client_fd, &sel_set)) && (ser_got < BUF_SIZE))
			{
				ret = recv(client_fd, (ser_buf + ser_got), (BUF_SIZE - ser_got), 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("recv from client failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "Client closed connection\n");
						exit(0);
					}
					ser_got += ret;
				}
			}
			if ((FD_ISSET(server_fd, &sel_set2)) && (ser_read < ser_got))
			{
				ret = send(server_fd, (ser_buf + ser_read), (ser_got - ser_read), 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("send to server failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "Nothing sended to server\n");
					}
					ser_read += ret;
					if (ser_read == ser_got)
					{
						ser_read = 0;
						ser_got = 0;
					}
				}
			}
			if ((FD_ISSET(client_fd, &sel_set2)) && (cl_read < cl_got))
			{
				ret = send(client_fd, (cl_buf + cl_read), (cl_got - cl_read), 0);
				if (ret < 0)
				{
					if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
						perror("send to client failed");
				}
				else
				{
					if (ret == 0)
					{
						fprintf(stderr, "Nothing sended to client\n");
					}
					cl_read += ret;
					if (cl_read == cl_got)
					{
						cl_read = 0;
						cl_got = 0;
					}
				}
			}
		}
	}
	
	return 0;
}
