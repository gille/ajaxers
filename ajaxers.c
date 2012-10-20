#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <syslog.h>
#include <string.h>
#include <netinet/in.h>

#include "protocol.h"

#define MAX_CHUNK 10000

struct task {
	pid_t pid;
	time_t sec;
	time_t lastpoll;
};

struct task *lookup(uint32_t id) {
	return NULL;
}

pid_t lookup_pid(uint32_t id) {
	struct task *t = lookup(id);
	
	return t?t->pid:-1;
}

static void spawn_task(const char *cmd) {
	struct task *task;

	task = malloc(sizeof(task));
	if((task->pid=fork()) == 0) {
		int i;
		char *real_cmd;
		int len = strlen(cmd); 
		real_cmd = malloc(len + 64); 
		free(task); /* we won't need it */
		snprintf(real_cmd, len+64, "%s > /tmp/ajax/%d.out", cmd, 
			 getpid()); 
		/* ok we're now in the child process */
		/* nuke all fds */
		for(i=0; i < 1024; i++)
			close(i); 
		system(real_cmd); 
	}
}

int recv_msg(int sockfd, struct msg *msg, int size, struct sockaddr_in *raddr) {
	unsigned int siz = sizeof(*raddr);
	recvfrom(sockfd, msg, size, 0, (struct sockaddr*)raddr, &siz);
	return 0;
}

int init_socket() {
	int sockfd;
	struct sockaddr_in saddr;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_LOOPBACK; 
	saddr.sin_port = htons(SERVER_PORT); 
	bind(sockfd, (struct sockaddr*)&saddr, sizeof(saddr));
	return sockfd;
}

void send_data(struct task *task, int sockfd, struct sockaddr_in *remote) {
}

int main(void) {
	int sockfd = init_socket();
	struct msg *msg;
	struct sockaddr_in raddr;
	
	msg = malloc(1234);
	memset(msg, 0, 32);
	
	/* holy muppet we must change this to a select */
	while(recv_msg(sockfd, msg, 1234, &raddr) == 0) {		
		switch(msg->cmd) {
		case MSG_EXEC:
			printf("received an EXEC cmd\n");
			spawn_task(msg->data);
			break;

		case MSG_GET: 
		{
			struct task *task = lookup(msg->id);
			printf("received a GET cmd\n");
			send_data(task, sockfd, &raddr); 
			break;
		}

		case MSG_KILL:
		{
			pid_t pid = lookup_pid(msg->id);
			printf("received a KILL cmd\n");
			if(pid != -1) {
			}
			break;
		}

		default:
			printf("uhm ajaxers received something we did not "
			       "expect.. %d %x\n ", msg->cmd, msg->cmd); 
			break;
		}
	}

	return 0;
}
