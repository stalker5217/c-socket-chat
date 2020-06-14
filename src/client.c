#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>

#define BUF_SIZE 10000
#define NAME_SIZE 20

void error_handling(char *buf);
void * send_msg(void * arg);
void * recv_msg(void * arg);
void interrupt_ctl(int sig);

char name[NAME_SIZE] = "[DEFAULT]";
int sock;
int interrupt;

int main() {
	struct sockaddr_in serv_addr;
	pthread_t snd_thread, rcv_thread;
	void * thread_return;
	char _ip[16];
	int _port;
	char temp[NAME_SIZE - 2];
	struct sigaction act;
	char boolean[2];
	int isblank = 0, i;

	act.sa_handler = interrupt_ctl;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, 0);

	printf("Enter Server IP: "); // 3.클라이언트는 실행시 포트 번호와 서버의 IP 주소, 자신의 id가 Standard input의 형태로 입력된다
	gets(_ip);

	printf("Enter Port Number: ");
	scanf("%d", &_port);
	getchar();

	sock = socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(_ip);
	serv_addr.sin_port = htons(_port);

	if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("connetct() error");

	while (1) {
		while (1) {
			isblank = 0;
			printf("Enter the Name(less than 10characters): ");
			gets(temp);

			if (interrupt == 1) {
				return -1;
			}

			for (i = 0; i < strlen(temp); i++) {
				if (temp[i] == ' ')
					isblank = 1;
			}
			if (strlen(temp) < 11 && isblank == 0) {
				break;
			}
			else
				printf("Name is too long or Name has blank\n");
		}
		sprintf(name, "[%s]", temp);
		write(sock, name, strlen(name));

		if (read(sock, boolean, 2) != 0) {
			boolean[1] = 0;
			if (atoi(boolean) == 0)
				break;
			else {
				printf("That name is already exist\n");
			}
		}
		else
			return -1;
	}
	//system("clear");
	pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
	pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
	pthread_detach(snd_thread);
	pthread_join(rcv_thread, &thread_return);
	
	close(sock);
	return 0;
}

void error_handling(char *buf) {
	fputs(buf, stderr);
	fputc('\n', stderr);
	exit(1);
}

void * send_msg(void * arg) {
	char msg[BUF_SIZE];
	int sock = *((int*)arg);
	
	while (1) {
		/*fprintf(stdout, "\033[7m >>> \033[m");*/
		fgets(msg, BUF_SIZE, stdin);
		write(sock, msg, strlen(msg));
	}
	
	return NULL;
}

void * recv_msg(void * arg){
	char msg[BUF_SIZE];
	int sock = *((int*)arg);
	
	int str_len;

	while(1){
		str_len = read(sock, msg, BUF_SIZE);
		if (str_len == -1)
			return (void*)-1;
		if (str_len == 0)
			break;
		msg[str_len] = 0;
		/*fprintf(stdout, "\033[1;1H");*/
		fputs(msg, stdout);
	}
	return NULL;
}

void interrupt_ctl(int sig) {
	if (sig == SIGINT) {
		interrupt = 1;
		shutdown(sock, SHUT_WR);
		close(sock);
		printf("\nClosed by ctrl - c\n");
	}
}