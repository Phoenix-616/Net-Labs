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
#include <signal.h>

#define CONNECTED (char)0
#define MSG (char)1
#define CONNECT_TO (char)2
#define GOT_MSG (char)3
#define DISCONNECT (char)4
#define MSG_SIZE 512
#define MSG_ID_RANGE 1000000
#define IT_IS_TIME_TO_DIE 3
#define TIMEOUT 2
#define MSG_QUEUE_LIMIT 40

typedef struct triada
{
	unsigned int id;
	int attempt;
	time_t last_attempt;
} triada;

typedef struct neighbor
{
	bool is_father;
	struct sockaddr_in addr;
	std::vector<triada> need_send;
	int death_counter;
} neighbor;

typedef struct message
{
	char * buf;
	unsigned int id;
	int non_send;
} message;

std::vector<neighbor> nods;
std::vector<message> msgs;
bool root = true;
int std_offset = sizeof(unsigned int) + sizeof(char);
bool work_end = false;

void shutting_down(int sig)
{
	fprintf(stderr, "\nExit...\n");
	work_end = true;
}

void clearing()
{
	for (int i = 0; i < msgs.size(); i++)
	{
		if (msgs.size() < MSG_QUEUE_LIMIT) break;
		if (*msgs[i].buf == MSG)
		{
			for (int j = 0; j < nods.size(); j++)
			{
				for (int k = 0; k < nods[j].need_send.size(); k++)
				{
					if (msgs[i].id == nods[j].need_send[k].id)
					{
						nods[j].need_send.erase(nods[j].need_send.begin() + k);
						break;
					}
				}
			}
			free(msgs[i].buf);
			msgs.erase(msgs.begin() + i);
			i--;
		}
	}
}

unsigned int msgConnected()												//Тук-тук, я к вам хочу
{
	clearing();
	message new_msg;
	if (!(new_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE)))
	{
		return 0;
	}
	new_msg.id = rand() / MSG_ID_RANGE + 1;
	new_msg.non_send = 1;
	*(new_msg.buf) = CONNECTED;
	memcpy((new_msg.buf + 1), &new_msg.id, sizeof(new_msg.id));
	msgs.push_back(new_msg);
	return new_msg.id;
}

unsigned int msgConnectTo(int ip, int port)								//Сообщение-команда переключиться на другого родителя
{
	clearing();
	message new_msg;
	fprintf(stderr, "Connect to %d %d\n", port, (nods.size() - 1));
	if (!(new_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE)))
	{
		return 0;
	}
	new_msg.id = rand() / MSG_ID_RANGE + 1;
	new_msg.non_send = nods.size() - 1;
	*(new_msg.buf) = CONNECT_TO;
	memcpy((new_msg.buf + 1), &new_msg.id, sizeof(new_msg.id));
	memcpy((new_msg.buf + std_offset), &ip, sizeof(ip));
	memcpy((new_msg.buf + std_offset + sizeof(int)), &port, sizeof(port));
	//fprintf(stderr, "Connect to %d in msg\n", *((int *)(new_msg.buf + std_offset + sizeof(int))));
	msgs.push_back(new_msg);
	return new_msg.id;
}

unsigned int msgDisconnect(bool r)
{
	clearing();
	message new_msg;
	if (!(new_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE)))
	{
		return 0;
	}
	new_msg.id = rand() / MSG_ID_RANGE + 1;
	new_msg.non_send = 1;
	*(new_msg.buf) = DISCONNECT;
	memcpy((new_msg.buf + 1), &new_msg.id, sizeof(new_msg.id));
	memcpy((new_msg.buf + std_offset), &r, sizeof(r));
	msgs.push_back(new_msg);
	return new_msg.id;
}

unsigned int msgGotMsg(unsigned int msg_id)								//Подтверждение получения сообщения
{
	clearing();
	message new_msg;
	if (!(new_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE)))
	{
		return 0;
	}
	new_msg.id = rand() / MSG_ID_RANGE + 1;
	new_msg.non_send = 1;	
	*(new_msg.buf) = GOT_MSG;
	memcpy((new_msg.buf + 1), &msg_id, sizeof(new_msg.id));
	msgs.push_back(new_msg);
	return new_msg.id;
}

unsigned int msgMsg(char * msg)											//Создание нового сообщения
{
	clearing();
	message new_msg;
	if (!(new_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE)))
	{
		return 0;
	}
	new_msg.id = rand() / MSG_ID_RANGE + 1;
	new_msg.non_send = nods.size();
	*(new_msg.buf) = MSG;
	memcpy((new_msg.buf + 1), &new_msg.id, sizeof(new_msg.id));
	memcpy((new_msg.buf + std_offset), msg, (MSG_SIZE - std_offset));
	msgs.push_back(new_msg);
	return new_msg.id;
}

neighbor createNod(int ip, int port, bool parent)
{
	neighbor new_nod;
	new_nod.addr.sin_family = AF_INET;
	new_nod.addr.sin_port = port;
	new_nod.addr.sin_addr.s_addr = ip;
	new_nod.is_father = parent;
	new_nod.death_counter = 0;
	return new_nod;
}

void pre_del_nod(int num)
{
	fprintf(stderr, "Pre del nod %d have %d\n", nods[num].addr.sin_port, nods[num].need_send.size());
	for (int i = 0; i < nods[num].need_send.size(); i++)
	{
		for (int j = 0; j < msgs.size(); j++)
		{
			if (nods[num].need_send[i].id == msgs[j].id)
			{
				msgs[j].non_send--;
				fprintf(stderr, "DO non_send-- %d\n", nods[num].addr.sin_port);
			}
		}
	}
}

int main(int argc, char * argv[])
{
	char *nickname = NULL;
	char nicklen;
	int sockFD;
	int ret, loss;
	struct sockaddr_in my_addr, addr;
	socklen_t addrlen = sizeof(addr);
	
	srand(time(NULL));
	
	if ((argc != 4) && (argc != 6))
	{
		fprintf(stderr, "Need loss %%, port, nickname (1-8 chars) and (optional) ip, port of father node\n");
		exit(0);
	}
	
	loss = atoi(argv[1]);
	fprintf(stderr, "loss = %d\n", loss);
	
	nickname = (char *)calloc(sizeof(char), (8 < strlen(argv[3]))?(9):(strlen(argv[3]) + 1));
	if (!nickname)
	{
		perror("malloc failed");
		exit(0);
	}
	memcpy(nickname, argv[3], (8 < strlen(argv[3]))?(9):(strlen(argv[3]) + 1));
	nicklen = strlen(nickname);
	
	sockFD = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockFD < 0)
	{
		perror("socket() failed");
		exit(0);
	}
	
	signal(SIGINT, shutting_down);
	
	memset((char *) &my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(atoi(argv[2]));
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	ret = bind(sockFD, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if (ret < 0)
	{
		perror("bind() failed");
		exit(0);
	}
	
	if (argc == 6)
	{
		neighbor new_nod = createNod(inet_addr(argv[4]), htons(atoi(argv[5])), false);		//parent node	
		triada temp;
		temp.id = msgConnected();
		temp.attempt = IT_IS_TIME_TO_DIE;///////////////////////////////
		temp.last_attempt = 0;
		new_nod.need_send.push_back(temp);
		nods.push_back(new_nod);
	}
	
	struct timeval selTime;
	fd_set readSet;
	
	message inc_msg;
	inc_msg.buf = (char *)calloc(sizeof(char), MSG_SIZE);
	if (inc_msg.buf == NULL)
	{
		perror("malloc() failed");
		exit(0);
	}
	
	char * str = (char *)calloc(sizeof(char), (MSG_SIZE - std_offset));
	if (str == NULL)
	{
		perror("malloc() failed");
		exit(0);
	}
	memcpy(str, nickname, nicklen + 1);
	*(str + nicklen) = '\n';
	free(nickname);
	
	fprintf(stderr, "My port %d root = %d\n", my_addr.sin_port, root);
	
	time_t curTime;
	
	bool was_there = false;
	
	while (1)
	{
		memset((char *) &addr, 0, sizeof(addr));
		FD_ZERO(&readSet);
		FD_SET(sockFD, &readSet);
		//if (!work_end)
			FD_SET(0, &readSet);
		selTime.tv_sec = 1;
		selTime.tv_usec = 0;
		
		ret = select((sockFD + 1), &readSet, NULL, NULL, &selTime);
		
		if (ret < 0 && !work_end)
		{
			perror("select() crashed");
			exit(0);
		}
		
		if (ret < 0 && work_end)
		{
			continue;
		}
		
		if (work_end && (nods.size() == 0))
		{
			fprintf(stderr, "Root == %d msgs == %d\n", root, msgs.size());
			for (int i = 0; i < msgs.size(); i++)
			{
				fprintf(stderr, "msg %d non send %d\n", *msgs[i].buf, msgs[i].non_send);
			}
			exit(0);
		}
		
		if (work_end && (nods.size() > 0) && !was_there)
		{
			was_there = true;
			triada temp;
			temp.attempt = IT_IS_TIME_TO_DIE;
			temp.last_attempt = 0;
			if (root)
			{
				temp.id = msgConnectTo(nods[0].addr.sin_addr.s_addr, nods[0].addr.sin_port);
				fprintf(stderr, "msg id = %d\n", temp.id);
				for (int i = 1; i < nods.size(); i++)
				{
					nods[i].need_send.push_back(temp);
					fprintf(stderr, "\nMAGIC %d\n", nods[i].need_send[nods[i].need_send.size() - 1]);
				}
				temp.id = msgDisconnect(true);
				nods[0].need_send.push_back(temp);
			}
			else
			{
				for (int i = 0; i < nods.size(); i++)
				{
					if (nods[i].is_father)
					{
						temp.id = msgDisconnect(false);
						nods[i].need_send.push_back(temp);
						temp.id = msgConnectTo(nods[i].addr.sin_addr.s_addr, nods[i].addr.sin_port);
						break;
					}
				}
				for (int i = 0; i < nods.size(); i++)
				{
					if (!nods[i].is_father)
					{
						nods[i].need_send.push_back(temp);
					}
				}
			}
		}
		
		if (ret > 0)
		{
			if (FD_ISSET(sockFD, &readSet))
			{
				//fprintf(stderr, "Got smth\n");
				if (inc_msg.buf != NULL)
				{
					addrlen = sizeof(addr);
					ret = recvfrom(sockFD, inc_msg.buf, MSG_SIZE, 0, (sockaddr *) &addr, &addrlen);
					int a = rand() % 100;
					fprintf(stderr, "loss = %d, rand = %d\n", loss, a);
					if ((ret  ==  MSG_SIZE) && (a >= loss))//////////////////////////////////////
					{
						int sender = -1;
						for (int i = 0; i < nods.size(); i++)
						{
							if ((addr.sin_port == nods[i].addr.sin_port) && (addr.sin_addr.s_addr == nods[i].addr.sin_addr.s_addr))
							{
								nods[i].death_counter = 0;
								sender = i;
								break;
							}
						}
						switch (*inc_msg.buf)
						{
							case CONNECTED:
							{
								if (work_end) break;
								fprintf(stderr, "CONNECTED!!! %d\n", addr.sin_port);
								
								unsigned int id;
								memcpy(&id, (inc_msg.buf + 1), sizeof(id));
								
								if (sender != -1)							//Нода, желающая подключиться порой успевает послать больше одного запроса на подключение, а нам две одинаковые записи в nods не нужны
								{
									triada temp;
									temp.id = msgGotMsg(id);
									temp.attempt = IT_IS_TIME_TO_DIE;
									temp.last_attempt = 0;
									nods[sender].need_send.push_back(temp);
									break;
								}
								neighbor nod = createNod(addr.sin_addr.s_addr, addr.sin_port, false);
								triada temp;
								temp.id = msgGotMsg(id);
								temp.attempt = IT_IS_TIME_TO_DIE;
								temp.last_attempt = 0;
								nod.need_send.push_back(temp);
								
								nods.push_back(nod);
								break;
							}
							case CONNECT_TO:
							{
								if ((sender == -1) || work_end) break;
								
								unsigned int id;
								memcpy(&id, inc_msg.buf + 1, sizeof(id));
								unsigned int oops = msgGotMsg(id);
								if (!oops) break;
								sendto(sockFD, msgs[msgs.size() - 1].buf, MSG_SIZE, 0, (struct sockaddr *) &nods[sender].addr, sizeof(nods[sender].addr));
								free(msgs[msgs.size() - 1].buf);
								msgs.erase(msgs.end() - 1);
								
								pre_del_nod(sender);
								nods.erase(nods.begin() + sender);
								
								int ip, port;
								
								memcpy(&ip, (inc_msg.buf + std_offset), sizeof(ip));
								memcpy(&port, (inc_msg.buf + std_offset + sizeof(int)), sizeof(port));
								
								//fprintf(stderr, "\nport %d\n", port);
								
								fprintf(stderr, "CONNECT_TO!!! %d\n", port);
								
								neighbor new_nod = createNod(ip, port, false);//////////////////////////ACHTUNG!!!
								root = true;
								triada temp;
								temp.attempt = IT_IS_TIME_TO_DIE;
								temp.id = msgConnected();
								temp.last_attempt = 0;
								new_nod.need_send.push_back(temp);
								nods.push_back(new_nod);
								break;
							}
							case GOT_MSG:
							{
								if (sender == -1) break;
								fprintf(stderr, "GOT_MSG!!! %d\n", addr.sin_port);
								unsigned int id;
								memcpy(&id, (inc_msg.buf + 1), sizeof(id));
								fprintf(stderr, "got msg id = %d\n", id);
								bool memorial_to_my_stupidity = false;
								for (int i = 0; i < msgs.size(); i++)
								{
									if (msgs[i].id == id)
									{
										if (*msgs[i].buf == CONNECTED)
										{
											nods[sender].is_father = true;
											root = false;
										}
										if ((*msgs[i].buf == CONNECT_TO) || (*msgs[i].buf == DISCONNECT))
										{
											fprintf(stderr, "Deleting nod %d\n", nods[sender].addr.sin_port);
											pre_del_nod(sender);
											nods.erase(nods.begin() + sender);
										}
										else
										{
											msgs[i].non_send--;
										}
										memorial_to_my_stupidity = true;
										break;
									}
								}
								if (memorial_to_my_stupidity) break;
								for (int i = 0; i < nods[sender].need_send.size(); i++)
								{
									if (nods[sender].need_send[i].id == id)
									{
										fprintf(stderr, "ERASE 1 %d by %d\n", id, nods[sender].addr.sin_port);
										nods[sender].need_send.erase(nods[sender].need_send.begin() + i);
										break;
									}
								}
								break;
							}
							case MSG:
							{
								if ((sender == -1) || work_end) break;
								fprintf(stderr, "MSG!!! %d\n", addr.sin_port);
								
								fprintf(stderr, "\n%s\n\n", (inc_msg.buf + std_offset));
								
								message resend;
								resend.buf = (char *)calloc(sizeof(char), MSG_SIZE);
								if (resend.buf != NULL)
								{
									memcpy(resend.buf, inc_msg.buf, MSG_SIZE);
									memcpy(&resend.id, (inc_msg.buf + 1), sizeof(resend.id));
									resend.non_send = nods.size() - 1;
									msgs.push_back(resend);
									for (int i = 0; i < nods.size(); i++)
									{
										if (i == sender)
										{
											triada temp;
											temp.id = msgGotMsg(resend.id);
											temp.attempt = IT_IS_TIME_TO_DIE;
											temp.last_attempt = 0;
											nods[i].need_send.push_back(temp);
										}
										else
										{
											triada temp;
											temp.id = resend.id;
											temp.attempt = IT_IS_TIME_TO_DIE;
											temp.last_attempt = 0;
											nods[i].need_send.push_back(temp);
										}
									}
								}
								break;
							}
							case DISCONNECT:
							{
								if (sender == -1) break;
								fprintf(stderr, "DISCONNECT!!! %d\n", addr.sin_port);
								
								unsigned int id;
								memcpy(&id, inc_msg.buf + 1, sizeof(id));
								unsigned int oops = msgGotMsg(id);
								if (!oops) break;
								sendto(sockFD, msgs[msgs.size() - 1].buf, MSG_SIZE, 0, (struct sockaddr *) &nods[sender].addr, sizeof(nods[sender].addr));
								free(msgs[msgs.size() - 1].buf);
								msgs.erase(msgs.end() - 1);
								
								bool t;
								memcpy(&t, (inc_msg.buf + std_offset), sizeof(t));
								if (t)
									root = t;
								pre_del_nod(sender);
								nods.erase(nods.begin() + sender);
								break;
							}
						}
					}
				}
			}
			
			if (FD_ISSET(0, &readSet))
			{
				
				/*fprintf(stderr, "ROOT = %d\n", root);
				for (int i = 0; i < nods.size(); i++)
				{
					fprintf(stderr, "Nod #%d port = %d is_father = %d\n", i, nods[i].addr.sin_port, nods[i].is_father);
				}*/
				int te;
				te = read(0, (str + nicklen + 1), (MSG_SIZE - std_offset - nicklen - 1));
				if (te > 0)
				{
					*(str + nicklen + te) = '\0';
					triada temp;
					temp.id = msgMsg(str);
					temp.attempt = IT_IS_TIME_TO_DIE;
					temp.last_attempt = 0;
					for (int i = 0; i < nods.size(); i++)
					{
						nods[i].need_send.push_back(temp);
					}
				}
			}
		}
		
		if (1)
		{
			bool skip;
			curTime = time(NULL);
			for (int i = 0; i < msgs.size(); i++)
			{
				if (msgs[i].non_send <= 0)
				{
					free(msgs[i].buf);
					msgs.erase(msgs.begin() + i);
					i--;
					continue;
				}
				skip = false;
				for (int j = 0; j < nods.size(); j++)
				{
					for (int k = 0; k < nods[j].need_send.size(); k++)
					{
						if (nods[j].need_send[k].id == 0)
						{
							fprintf(stderr, "ERASE 2 %d\n", nods[j].need_send[k].id);
							nods[j].need_send.erase(nods[j].need_send.begin() + k);
							k--;
							continue;
						}
						if ((msgs[i].id == nods[j].need_send[k].id) && ((curTime - nods[j].need_send[k].last_attempt) > TIMEOUT))
						{
							addrlen = sizeof(nods[j].addr);
							ssize_t oops = sendto(sockFD, msgs[i].buf, MSG_SIZE, 0, (struct sockaddr *) &(nods[j].addr), addrlen);
							if (oops != MSG_SIZE) continue;
							fprintf(stderr, "Sending %d to %d...\n", *msgs[i].buf, nods[j].addr.sin_port);
							nods[j].need_send[k].attempt--;
							nods[j].need_send[k].last_attempt = curTime;
							if (*msgs[i].buf == GOT_MSG)
							{
								fprintf(stderr, "ERASE 3 %d\n", nods[j].need_send[k]);
								nods[j].need_send.erase(nods[j].need_send.begin() + k);
								
								free(msgs[i].buf);
								msgs.erase(msgs.begin() + i);
								i--;
								skip = true;
								break;
							}
							if (nods[j].need_send[k].attempt == 0)
							{
								
								if ((*msgs[i].buf == CONNECT_TO) || (*msgs[i].buf == DISCONNECT))
								{
									nods[j].death_counter = IT_IS_TIME_TO_DIE;
								}
								
								msgs[i].non_send--;
								if (msgs[i].non_send <= 0)
								{
									free(msgs[i].buf);
									msgs.erase(msgs.begin() + i);
									i--;
									skip = true;
								}
								
								nods[j].death_counter++;
								if (nods[j].death_counter >= IT_IS_TIME_TO_DIE)
								{
									pre_del_nod(j);
									nods.erase(nods.begin() + j);
									j--;
									break;
								}
								else
								{
									fprintf(stderr, "ERASE 4 %d\n", nods[j].need_send[k]);
									nods[j].need_send.erase(nods[j].need_send.begin() + k);
									k--;
								}
							}
						}
					}
					if (skip) break;
				}
			}
		}
	}
	
	free(inc_msg.buf);
	free(str);
	
	return 0;
}
