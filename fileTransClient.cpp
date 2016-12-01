#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#define BUF_SIZE 255

char buf[BUF_SIZE];

int main(int argc, char * argv[])
{
	int sockfd, filefd;
	struct sockaddr_in serv_addr;
	char * filename;
	off_t filesize;
	
	if (argc < 4)
	{
		printf("For start need ip, port and filename\n");
		exit(0);
	}
	
	filefd = open(argv[3], O_RDONLY);
	if (filefd < 0)
	{
		perror("Cannot open file");
		exit(0);
	}
	
	filename = basename(argv[3]);
	filesize = lseek(filefd, 0, SEEK_END);
	int curPos = lseek(filefd, 0, SEEK_SET);
	fprintf(stderr, "Start read from %d\n", curPos);
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		perror("Cannot create socket");
		exit(0);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(atoi(argv[2]));
	
	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
    {
		perror("Cannot connect to server");
		exit(0);
	}
	
	off_t ret = strlen(filename);
	fprintf(stderr, "NAME %d %s\n", ret, filename);
	int chck;
	chck = send(sockfd, &filesize, sizeof(filesize), 0);
	if (chck != sizeof(filesize)) perror("FUCK1!!!!");
	chck = send(sockfd, &ret, sizeof(ret), 0);
	if (chck != sizeof(ret)) perror("FUCK2!!!!");
	//sleep(1);
	chck = send(sockfd, filename, (ret + 1), 0);
	if (chck != (ret + 1)) perror("FUC3!!!!");
	
	recv(sockfd, &ret, sizeof(ret), 0);
	if (ret != 0)
	{
		fprintf(stderr, "Server doesn't accept file\n");
		exit(0);
	}
	
	int readByte;
	
	ret = 0;
	
    while ((readByte = read(filefd, buf, BUF_SIZE)) > 0)
    {
		int dif;
		int t;
		fprintf(stderr, "read() read %d\n", readByte);
		while (ret < readByte)
		{
			dif = readByte - ret;
			t = send(sockfd, (buf + ret), dif, 0);
			if (t <= 0)
			{
				fprintf(stderr, "Smth goes wrong\n");
				if (t < 0) perror("Transfer failed");
				exit(0);
			}
			filesize -= t;
			fprintf(stderr, "Sending... %d\n", filesize);
			ret = ret + t;
		}
		ret = 0;
	}
	if (readByte < 0)
	{
		perror("WTF!!!");
	}
	if (readByte == 0)
	{
		fprintf(stderr, "Sended all\n");
	}
	ret = 1;
	recv(sockfd, &ret, sizeof(ret), 0);
	if (ret == 0) printf("Done\n");
	else printf("Transmission failed\n");
	return 0;
}
