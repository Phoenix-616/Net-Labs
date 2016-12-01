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

int main(int argc, char * argv[])
{
	int sockfd;
	struct sockaddr_in serv_addr;
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(14204);
	
	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
        fprintf(stderr, "ERROR connecting\n");
        
    while (1)
    {
		write(sockfd, buf, 255);
	}
	
	return 0;
}
