#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

char buf[255];

typedef struct sp
{
	unsigned long total;
	unsigned long current;
	int sockfd;
	int start_time;
	struct sockaddr_in cli_addr;
	socklen_t clilen;
} speedNode;

int main()
{
	std::vector<speedNode> nods;
	int servSockFD;
	struct sockaddr_in serv_addr;
	struct timeval selTime;
	
	/*make addr*/
	
	servSockFD = socket(AF_INET, SOCK_STREAM, 0);
	
	bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);/*inet_addr("192.168.43.112");*/
    serv_addr.sin_port = htons(14204);
	
	bind(servSockFD, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	
	unsigned long time_stop, time_start = time(NULL);
	int selMark;
	int maxFD;
	unsigned long readByte;
	fd_set sockSet;
	
	listen(servSockFD, 100);
	
	while (1)
	{
		FD_ZERO(&sockSet);
		FD_SET(servSockFD, &sockSet);
		maxFD = servSockFD;
		for (int i = 0; i < nods.size(); i++)
		{
			if (nods[i].sockfd > maxFD) maxFD = nods[i].sockfd;
			FD_SET(nods[i].sockfd, &sockSet);
		}
		selTime.tv_sec = 1;
		selTime.tv_usec = 0;
		selMark = select(maxFD + 1, &sockSet, NULL, NULL, &selTime);
		//fprintf(stderr, "Select OK\n");
		
		if (selMark > 0)
		{
			for (int i = 0; i < nods.size(); i++)
			{
				if (FD_ISSET(nods[i].sockfd, &sockSet))
				{
					readByte = read(nods[i].sockfd, buf, 255);
					nods[i].current += readByte;
					nods[i].total += readByte;
				}
			}
			
			if (FD_ISSET(servSockFD, &sockSet))
			{
				fprintf(stderr, "New client locked\n");
				speedNode newArr;
				newArr.total = 0;
				newArr.current = 0;
				newArr.start_time = time(NULL);
				newArr.clilen = sizeof(newArr.cli_addr);
				newArr.sockfd = accept(servSockFD, (struct sockaddr *) &(newArr.cli_addr), &(newArr.clilen));
				nods.push_back(newArr);
				//fprintf(stderr, "New client accepted\n");
			}
		}
		//fprintf(stderr, "Select check OK\n");
		
		time_stop = time(NULL);
		
		if ((time_stop - time_start) > 0)
		{
			fprintf(stderr, "New itteration\n");
			for (int i = 0; i < nods.size(); i++)
			{
				//fprintf(stderr, "Ready to print %d\n", (time_stop - nods[i].start_time));
				fprintf(stderr, "Client #%d curSp = %d KB/s aveSp = %d KB/s\n", i, nods[i].current / 1024, (nods[i].total / 1024) / (time_stop - nods[i].start_time + 1));
				nods[i].current = 0;
			}
			time_start = time(NULL);
		}
	}
	
	return 0;
}
