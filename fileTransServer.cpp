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
#include <malloc.h>

#define BUF_SIZE 255

enum STATUS {FILESIZE, NAMESIZE, NAME, FILE_ANSWER, GET_DATA, DATA_ANSWER};

char buf[BUF_SIZE];

typedef struct sp
{
	int sockFD;
	int fileFD;
	char * filename;
	off_t filenamesize;
	off_t filesize;
	STATUS status;
	bool check;
} speedNode;

std::vector<speedNode> nods;

void del_nod(int num)
{
	close(nods[num].sockFD);
	if (nods[num].fileFD >= 0)
	{
		if (!nods[num].check)
		{
			if (!remove(nods[num].filename))
			{
				fprintf(stderr, "File deleted\n");
			}
			else
			{
				perror("File deliting problem");
			}
		}
		close(nods[num].fileFD);
	}
	if (nods[num].filename != NULL) 
		free(nods[num].filename);
	if (nods[num].check)
		fprintf(stderr, "Transfer SUCCESS\n");
	else
		fprintf(stderr, "Transfer FAILED\n");
	nods.erase(nods.begin() + num);
}

int main(int argc, char * argv[])
{
	std::vector<int> to_del;
	int servSockFD, uploadFD;
	struct sockaddr_in serv_addr;
	struct timeval selTime;
	int ret;

	fprintf(stderr, "server 2\n");
	/*make addr*/
	
	if (argc < 2)
	{
		fprintf(stderr, "Need port\n");
		exit(0);
	}
	
	servSockFD = socket(AF_INET, SOCK_STREAM, 0);
	if (servSockFD < 0)
	{
		perror("Cannot create socket");
		exit(0);
	}
	
	bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
	
	ret = bind(servSockFD, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret < 0)
	{
		perror("bind() failed");
		exit(0);
	}
	
	int selMark;
	int maxFD;
	int writeByte;
	fd_set readSet, writeSet;
	
	ret = listen(servSockFD, 100);
	if (ret < 0)
	{
		perror("listen() failed");
		exit(0);
	}
	
	while (1)
	{
		FD_ZERO(&readSet);
		FD_ZERO(&writeSet);
		FD_SET(servSockFD, &readSet);
		maxFD = servSockFD;
		for (int i = 0; i < nods.size(); i++)
		{
			if (nods[i].sockFD > maxFD) maxFD = nods[i].sockFD;
			if ((nods[i].status == FILE_ANSWER) || (nods[i].status == DATA_ANSWER))
				FD_SET(nods[i].sockFD, &writeSet);
			else
				FD_SET(nods[i].sockFD, &readSet);
		}
		selTime.tv_sec = 1;
		selTime.tv_usec = 0;
		selMark = select(maxFD + 1, &readSet, &writeSet, NULL, &selTime);
		//fprintf(stderr, "Select OK\n");
		
		to_del.clear();
		
		if (selMark > 0)
		{
			if (FD_ISSET(servSockFD, &readSet))
			{
				printf("New connection accepted\n");
				speedNode nod;
				nod.sockFD = accept(servSockFD, NULL, NULL); 
				nod.status = FILESIZE;
				nod.check = true;
				nod.fileFD = -1;
				nod.filename = NULL;
				if (nod.sockFD >= 0)
					nods.push_back(nod);
			}
			for (int i = 0; i < nods.size(); i++)
			{
				if (FD_ISSET(nods[i].sockFD, &readSet))					//FILESIZE, NAMESIZE, NAME, FILE_ANSWER, GET_DATA, DATA_ANSWER
				{
					switch (nods[i].status)
					{
						case FILESIZE:
						{
							fprintf(stderr, "FILESIZE\n");
							ret = recv(nods[i].sockFD, &nods[i].filesize, sizeof(off_t), 0);
							if (ret == 0) 
							{
								fprintf(stderr, "FILESIZE: connection closed by peer");
								nods[i].check = false;
								del_nod(i);
								i--;
								continue;
							}
							fprintf(stderr, "File size = %d\n", nods[i].filesize);
							nods[i].status = NAMESIZE;
							break;
						}
						case NAMESIZE:
						{
							fprintf(stderr, "NAMESIZE\n");
							ret = recv(nods[i].sockFD, &nods[i].filenamesize, sizeof(off_t), 0);
							if (ret == 0) 
							{
								fprintf(stderr, "NAMESIZE: connection closed by peer\n");
								nods[i].check = false;
								del_nod(i);
								i--;
								continue;
							}
							nods[i].filename = (char *)malloc(nods[i].filenamesize + 1);
							if (nods[i].filename == NULL)
							{
								fprintf(stderr, "Cannot allocate mem for name\n");
								nods[i].check = false;
							}
							fprintf(stderr, "File's name size = %d\n", nods[i].filenamesize);
							nods[i].status = NAME;
							break;
						}
						case NAME:
						{
							fprintf(stderr, "NAME\n");
							if (nods[i].check)
							{
								ret = recv(nods[i].sockFD, nods[i].filename, (nods[i].filenamesize + 1), 0);
								fprintf(stderr, "1File name = %s %d %d\n", nods[i].filename, ret, *nods[i].filename);
								if (ret == 0) 
								{
									fprintf(stderr, "NAME: connection closed by peer\n");
									nods[i].check = false;
									del_nod(i);
									i--;
									continue;
								}
								if (ret != (nods[i].filenamesize + 1))
								{
									fprintf(stderr, "ret != filenamesize + 1\n");
									nods[i].check = false;
								}
								else
								{
									fprintf(stderr, "File name = %s %d %c\n", nods[i].filename, ret, *nods[i].filename);
									if (access(nods[i].filename, F_OK) != 0)
									{
										nods[i].fileFD = creat(nods[i].filename, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
										if (nods[i].fileFD < 0)
										{
											perror("File creation failed");
											nods[i].check = false;
										}
									}
									else
									{
										fprintf(stderr, "File exists\n");
										nods[i].check = false;
									}
								}
							}
							nods[i].status = FILE_ANSWER;
							break;
						}
						case GET_DATA:
						{
							ret = recv(nods[i].sockFD, buf, BUF_SIZE, 0);
							nods[i].filesize -= ret;
							//fprintf(stderr, "GET_DATA %d\n", nods[i].filesize);
							if (ret <= 0) 
							{
								if (ret < 0) perror("recv() failed");
								nods[i].check = false;
								del_nod(i);
								i--;
								continue;
							}
							else
							{
								writeByte = 0;
								int dif;
								int t;
								bool f = false;
								while (writeByte < ret)
								{
									dif = ret - writeByte;
									t = write(nods[i].fileFD, (buf + writeByte), dif);
									if (writeByte < 0)
									{
										perror("write() failed");
										f = true;
										del_nod(i);
										i--;
										break;
									}
									writeByte += t;
								}
								if (f) continue;
								if (nods[i].filesize <= 0)
									nods[i].status = DATA_ANSWER;
							}
							break;
						}
					}
				}
				if (FD_ISSET(nods[i].sockFD, &writeSet))
				{
					switch (nods[i].status)
					{
						case FILE_ANSWER:
						{
							fprintf(stderr, "FILE_ANSWER\n");
							if (nods[i].check)
							{
								ret = 0;
								send(nods[i].sockFD, &ret, sizeof(ret), 0);
								nods[i].status = GET_DATA;
								printf("Downloading %s\n", nods[i].filename);
							}
							else
							{
								ret = 1;
								send(nods[i].sockFD, &ret, sizeof(ret), 0);
								del_nod(i);
								i--;
								continue;
							}
							break;
						}
						case DATA_ANSWER:
						{
							fprintf(stderr, "DATA_ANSWER\n");
							if (nods[i].check)
							{
								ret = 0;
								send(nods[i].sockFD, &ret, sizeof(ret), 0);
								del_nod(i);
								i--;
								continue;
							}
							else
							{
								ret = 1;
								send(nods[i].sockFD, &ret, sizeof(ret), 0);
								del_nod(i);
								i--;
								continue;
							}
							break;
						}
					}
				}
			}
		}
	}
	
	return 0;
}
