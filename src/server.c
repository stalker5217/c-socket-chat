#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>

#define BUF_SIZE 10000
#define MAX_CLNT 256
#define MAX_ROOM 256
#define NAME_SIZE 20
#define LISTEN 30

typedef enum _state {
	ERROR, EXIT, QUIT, HELP, VOIDTOCHAT, CHATTOCHAT, ROOM, CLIENT, WHISPER, INTERRUPT
} state;

typedef struct _clnt_info {
	char name[NAME_SIZE];
	int fd;
	int room;
} clnt_info;

typedef struct _room_info {
	char name[NAME_SIZE];
	int clnt_socks[MAX_CLNT];
	int clnt_cnt;
} room_info;

void error_handling(char *buf);
void * handle_clnt(void *arg);
void * handle_server(void *arg);
void send_msg(char *msg, clnt_info clnt_sock, int room_number);
int chat_room(clnt_info, int room_number);
void interrupt_ctl(int);
int command(clnt_info, char *);

room_info room[MAX_ROOM];
clnt_info clnt[MAX_CLNT];

int room_cnt;
int client_cnt;

pthread_mutex_t mutx[MAX_ROOM]; // 각 채팅방 내부 동기화를 위한 뮤텍스
pthread_mutex_t mutx_r; // 전체 채팅방을 관리하기 위한 뮤텍스
pthread_mutex_t mutx_c; // 전체 클라이언트를 관리하기 위한 뮤텍스

int serv_sock;

char help_message[BUF_SIZE] = "\
--------------------------------------------------------\n\
/help             :  Show help message\n\
/join [Channel]   :  join Channel which name is [----]\n\
/room             :  Information for active room\n\
/client           :  Information for active user\n\
/w [ID] [TEXT]    :  Whispering to ID\n\
/quit             :  go to void Channel\n\
/exit             :  exit program only on void Channel\n\
--------------------------------------------------------\n";

char temp[BUF_SIZE];
char voidchannel[100] = "This is the void channel not to chat\n";

int main() {
	int clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	socklen_t clnt_adr_sz;
	int port;
	int i;
	pthread_t t_id;
	struct sigaction act;

	act.sa_handler = interrupt_ctl;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, 0);

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);

	printf("Enter port number: ");
	scanf("%d", &port); // 1. 서버는 실행시 포트 번호가 standard input의 형태로 입력된다.	

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(port);

	if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");

	if (listen(serv_sock, LISTEN) == -1)
		error_handling("listen() error");

	for (i = 0; i < MAX_ROOM; i++)
		pthread_mutex_init(&mutx[i], NULL);
	pthread_mutex_init(&mutx_r, NULL);
	pthread_mutex_init(&mutx_c, NULL);

	for (i = 0; i < MAX_ROOM; i++)
		room[i].clnt_cnt = 0;

	for (i = 0; i < MAX_CLNT; i++)
		clnt[i].fd = -1;

	puts("Server is activated....");

	pthread_create(&t_id, NULL, handle_server, NULL); // if enter 'q', server is closed
	pthread_detach(t_id);

	while (1) {
		clnt_adr_sz = sizeof(clnt_adr);
		clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
		pthread_create(&t_id, NULL, handle_clnt, (void*)&clnt_sock);
		pthread_detach(t_id);
	}
	close(serv_sock);
	return 0;
}

void error_handling(char *buf) {
	fputs(buf, stderr);
	fputc('\n', stderr);
	exit(1);
}

void * handle_server(void * arg) { // 2. 사용자가 'q'를 입력할 시에 종료된다.
	char buf[BUF_SIZE];
	int i;

	while (1) {
		fgets(buf, BUF_SIZE, stdin);

		if (strcmp(buf, "q\n") == 0 || strcmp(buf, "Q\n") == 0) {
			pthread_mutex_lock(&mutx_c);
			for (i = 0; i<client_cnt; i++)
			{
				if (clnt[i].fd != -1)
					write(clnt[i].fd, "Server closed\n", 50);
			}
			pthread_mutex_unlock(&mutx_c);
			puts("server closed");
			close(serv_sock);
			exit(1);
		}

		else if (strcmp(buf, "/client\n") == 0) {
			pthread_mutex_lock(&mutx_c);
			printf("Current Clinet Count >> %d\n", client_cnt);
			for (i = 0; i < MAX_CLNT; i++) {
				if (clnt[i].fd > 0) {
					printf("ID:%10s\n", clnt[i].name);
				}
			}
			pthread_mutex_unlock(&mutx_c);
		}

		else if (strcmp(buf, "/room\n") == 0) {
			pthread_mutex_lock(&mutx_r);
			printf("Current Room Count >> %d\n", room_cnt);
			for (i = 0; i < MAX_ROOM; i++) {
				if (room[i].clnt_cnt > 0) {
					printf("ROOM:%10s\n", room[i].name);
				}
			}
			pthread_mutex_unlock(&mutx_r);
		}
		memset(buf, 0, sizeof(buf));
	}
}

void * handle_clnt(void *arg) {
	clnt_info clnt_sock;
	int str_len = 0, i;
	char msg[BUF_SIZE];
	char roomName[NAME_SIZE];
	int exist;
	state s = ERROR;
	int boolean = 1;
	char voidchannel[100] = "This is the void channel not to chat\n";
	char t[BUF_SIZE];

	clnt_sock.fd = *((int*)arg);
	clnt_sock.room = -1;

	while (boolean == 1) {
		boolean = 0;
		memset(msg, 0, BUF_SIZE);
		if (read(clnt_sock.fd, msg, BUF_SIZE) != 0) { // 처음 수신 되는거는 클라이언트의 닉네임
			pthread_mutex_lock(&mutx_c);
			for (i = 0; i < client_cnt; i++) {
				if (strcmp(msg, clnt[i].name) == 0) {
					boolean = 1;
					write(clnt_sock.fd, "1\n", 2);
					break;
				}
			}
			pthread_mutex_unlock(&mutx_c);
		}
		else {
			close(clnt_sock.fd);
			printf("%s is disconnected\n", clnt_sock.name);
			return NULL;
		}
	}
	write(clnt_sock.fd, "0\n", 2);
	sprintf(clnt_sock.name, "%s", msg);
	printf("%s is connected\n", clnt_sock.name); // 4. 서버에는 새로운 클라이언트가 접속될 때 마다 새로 접속된 클라이언트의 접속을 알리는 메시지가 출력된다.

	pthread_mutex_lock(&mutx_c);
	for (i = 0; i < MAX_CLNT; i++) {
		if (clnt[i].fd < 0) {
			clnt[i].fd = clnt_sock.fd;
			sprintf(clnt[i].name, "%s", clnt_sock.name);
			break;
		}
	}
	client_cnt++;
	pthread_mutex_unlock(&mutx_c);

	sprintf(t, "%s%s", voidchannel, help_message);
	write(clnt_sock.fd, t, BUF_SIZE);

	while (1) {
		if (s == CHATTOCHAT) {
			memset(msg, 0, BUF_SIZE);
			sprintf(msg, "%s", temp);
			str_len = strlen(msg);
			s = VOIDTOCHAT;
		}
			
		else {
			memset(msg, 0, BUF_SIZE);
			str_len = read(clnt_sock.fd, msg, BUF_SIZE);
		}
		
		if (s == INTERRUPT || str_len == 0) {
			pthread_mutex_lock(&mutx_c);
			for (i = 0; i < MAX_CLNT; i++)
				if (clnt[i].fd == clnt_sock.fd)
					break;

			clnt[i].fd = -1;
			memset(clnt[i].name, 0, NAME_SIZE);
			clnt[i].room = -1;
			client_cnt--;
			pthread_mutex_unlock(&mutx_c);
			break;
		}
		else {
			msg[str_len] = 0;
			exist = 0;
		}

		if (msg[0] == '/') {
			s = command(clnt_sock, msg);

			if (s == EXIT) {
				break;
			}

			else if (s == VOIDTOCHAT) {
				for (i = 6; i < strlen(msg) - 1; i++)
					roomName[i - 6] = msg[i];
				roomName[i] = '\0';

				pthread_mutex_lock(&mutx_r);
				for (i = 0; i < MAX_ROOM; i++) {
					if (strcmp(roomName, room[i].name) == 0) { // 해당 이름과 같은 방이 존재하면,
						exist = 1;
						clnt_sock.room = i;
						pthread_mutex_unlock(&mutx_r);
						s = chat_room(clnt_sock, i); // 해당 방번호로 입장
						if(s != CHATTOCHAT)
							write(clnt_sock.fd, "Here is void channel.\nif you want to terminate program, Enter '/exit'\n", 71);
						break;
					}
				}

				if (exist == 0) { // 해당 이름과 같은 방이 존재하지 않으면 방 생성
					for (i = 0; i < MAX_ROOM; i++)
						if (room[i].clnt_cnt == 0)
							break;

					sprintf(room[i].name, "%s", roomName);
					room[i].clnt_cnt = 0;
					room_cnt++;
					clnt_sock.room = i;
					printf("New chatting room creation: %s\n", room[i].name);
					//printf("Current Room Count >> %d\n", room_cnt);
					pthread_mutex_unlock(&mutx_r);
					s = chat_room(clnt_sock, i);
					if (s != CHATTOCHAT)
						write(clnt_sock.fd, "Here is void channel.\nif you want to terminate program, Enter '/exit'\n", 71);
				}

				clnt_sock.room = -1;
			}
		}

		else { // 일반메시지시 서버와의 통신(에코 서비스)
			//fputs(msg, stdout);
			memset(t, 0, BUF_SIZE);
			sprintf(t, "%s%s", msg, voidchannel);
			write(clnt_sock.fd, t, BUF_SIZE);
		}
	}

	close(clnt_sock.fd);
	printf("%s is disconnected\n", clnt_sock.name);

	return NULL;
}

int chat_room(clnt_info clnt_sock, int room_number) {
	int str_len, i;
	char msg[BUF_SIZE];
	char notice[BUF_SIZE];
	enum _state s;

	clnt_sock.room = room_number;
	pthread_mutex_lock(&mutx[room_number]);
	room[room_number].clnt_socks[room[room_number].clnt_cnt] = clnt_sock.fd; // 해당 방에 클라이언트 추가
	room[room_number].clnt_cnt++; // 해당 방의 총 클라이언트 수 증가
	sprintf(notice, "%s is enter\n", clnt_sock.name);
	for (i = 0; i < room[room_number].clnt_cnt; i++) // 해당 클라이언트가 입장 했음을 방에 뿌려준다
		write(room[room_number].clnt_socks[i], notice, strlen(notice));
	pthread_mutex_unlock(&mutx[room_number]);

	while (1) {// 해당 방 인원들과의 채팅
		memset(msg, 0, BUF_SIZE);
		if ((str_len = read(clnt_sock.fd, msg, BUF_SIZE - NAME_SIZE)) == 0){
			s = INTERRUPT;
			break;
		}
		if (msg[0] == '/') {
			s = command(clnt_sock, msg);

			if (s == QUIT)
				break;

			else if (s == CHATTOCHAT) {
				memset(temp, 0, BUF_SIZE);
				sprintf(temp, "%s", msg);
				break;
			}
		}

		else
			send_msg(msg, clnt_sock, room_number);
	}

	clnt_sock.room = -1;

	pthread_mutex_lock(&mutx[room_number]);

	sprintf(notice, "%s leave the room\n", clnt_sock.name);
	for (i = 0; i < room[room_number].clnt_cnt; i++) {
		if (room[room_number].clnt_socks[i] != clnt_sock.fd)
			write(room[room_number].clnt_socks[i], notice, strlen(notice));
	}
	
	for (i = 0; i < room[room_number].clnt_cnt; i++) { // 방에서 퇴장시 방 정보에서 해당 클라이언트 정보 제거
		if (clnt_sock.fd == room[room_number].clnt_socks[i]) {
			while (i < room[room_number].clnt_cnt - 1) {
				room[room_number].clnt_socks[i] = room[room_number].clnt_socks[i + 1];
				i++;
			}
			break;
		}
	}
	
	room[room_number].clnt_cnt--;
	if (room[room_number].clnt_cnt == 0) {
		printf("Room \"%s\" is deleted\n", room[room_number].name);
		memset(room[room_number].name, 0, sizeof(room[room_number].name));
		room_cnt--;
	}
	pthread_mutex_unlock(&mutx[room_number]);
	
	return s;
}

void send_msg(char *msg, clnt_info clnt_sock, int room_num) {
	int i;
	char m[BUF_SIZE];

	memset(m, 0, sizeof(m));
	sprintf(m, "%s: %s", clnt_sock.name, msg);
	pthread_mutex_lock(&mutx[room_num]);
	for (i = 0; i < room[room_num].clnt_cnt; i++) {
		if(clnt_sock.fd != room[room_num].clnt_socks[i])
			write(room[room_num].clnt_socks[i], m, BUF_SIZE);
	}
	pthread_mutex_unlock(&mutx[room_num]);
}

void interrupt_ctl(int sig) {
	int i;
	if (sig == SIGINT) {
		pthread_mutex_lock(&mutx_c);
		for (i = 0; i<client_cnt; i++)
		{
			if (clnt[i].fd != -1)
				write(clnt[i].fd, "Server closed by ctrl - c\n", 50);
		}
		
		pthread_mutex_unlock(&mutx_c);
		puts("server closed");
		close(serv_sock);
		exit(1);
	}
}

int command(clnt_info clnt_sock, char * msg) {
	int i;
	char m[BUF_SIZE];
	char command_error[100] = "This command is invalid. Please refer '/help'\n";

	if (clnt_sock.room == -1 && strcmp(msg, "/exit\n") == 0) {
		pthread_mutex_lock(&mutx_c);
		for (i = 0; i < MAX_CLNT; i++)
			if (clnt[i].fd == clnt_sock.fd)
				break;

		clnt[i].fd = -1;
		memset(clnt[i].name, 0, NAME_SIZE);
		clnt[i].room = -1;
		client_cnt--;
		pthread_mutex_unlock(&mutx_c);

		shutdown(clnt_sock.fd, SHUT_WR);
		return EXIT;
	}

	else if (strcmp(msg, "/quit\n") == 0) {
		if(clnt_sock.room > -1)
			return QUIT;
		else {
			write(clnt_sock.fd, "Here is void Channel.\nIf you want to terminate program, Enter '/exit'\n", 71);
			return ERROR;
		}
	}

	else if (strcmp(msg, "/help\n") == 0) {
		write(clnt_sock.fd, help_message, BUF_SIZE);

		return HELP;
	}

	else if (strncmp(msg, "/join ", 6) == 0) {
		if (clnt_sock.room > -1)
			return CHATTOCHAT;
		else
			return VOIDTOCHAT;
	}

	else if (strcmp(msg, "/room\n") == 0) {
		memset(m, 0, BUF_SIZE);
		pthread_mutex_lock(&mutx_r);
		sprintf(m, "Current Room Count >> %d\n", room_cnt);
		for (i = 0; i < MAX_ROOM; i++)
		{
			if (room[i].clnt_cnt > 0)
			{
				sprintf(m, "%sROOM:%10s\n", m, room[i].name);
			}
		}
		pthread_mutex_unlock(&mutx_r);
		sprintf(m, "%s\n", m);
		write(clnt_sock.fd, m, sizeof(m));

		return ROOM;
	}

	else if (strcmp(msg, "/client\n") == 0) {
		memset(m, 0, BUF_SIZE);
		pthread_mutex_lock(&mutx_c);
		sprintf(m, "Current Client Count >> %d\n", client_cnt);
		for (i = 0; i < MAX_CLNT; i++)
		{
			if (clnt[i].fd >= 0)
			{
				sprintf(m, "%sCLIENT:%10s\n", m, clnt[i].name);
			}
		}
		pthread_mutex_unlock(&mutx_c);
		sprintf(m, "%s\n", m);
		write(clnt_sock.fd, m, sizeof(m));

		return CLIENT;
	}

	else if (strncmp(msg, "/w ", 3) == 0) {
		char name[NAME_SIZE];
		char text[BUF_SIZE];
		char org[BUF_SIZE];
		char *temp;
		int name_len;
		int fd = -1;

		sprintf(org, "%s", msg);
		temp = strtok(org, " ");
		temp = strtok(NULL, " ");
		name_len = strlen(temp);
		sprintf(name, "[%s]", temp);
	
		for (i = 0; i < MAX_CLNT; i++) {
			if (strcmp(name, clnt[i].name) == 0) {
				fd = clnt[i].fd;
				break;
			}
		}
		if (fd > -1) {
			temp = &msg[name_len + 4];
			sprintf(text, "WHISPER by %s: %s", clnt_sock.name, temp);
			write(fd, text, BUF_SIZE);
		}
		else {
			write(clnt_sock.fd, "There is no client\n", BUF_SIZE);
		}

		return WHISPER;
	}

	else {
		write(clnt_sock.fd, command_error, BUF_SIZE);

		return ERROR;
	}
}