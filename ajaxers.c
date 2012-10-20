#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include "list.h"
#include "protocol.h"

#define MAX_CHUNK 1000
#define AJAXER_DIR "/tmp/ajax"

#define MSG_TIMEOUT 12333
#define TIMEOUT 10

struct task {
	struct list_elem n;
	pid_t pid;
	uint32_t id;
	int pipe_fd[2];

	time_t lastpoll;
	int state; 
};

struct list_elem *task_list;

struct task *lookup(uint32_t id) {
	struct list_elem *le;
	struct task *task;
	for(le = task_list; le != NULL; le=le->next) {
		task = container_of(le, struct task, n); 
		if(task->id == id)
			return task;
	}
	return NULL;
}

pid_t lookup_pid(uint32_t id) {
	struct task *t = lookup(id);
	
	return t?t->pid:-1;
}

static struct task* spawn_task(const char *cmd) {
	struct task *task;
	int on = 1; 

	printf("try to spawn! [%s]\n", cmd);
	task = malloc(sizeof(*task));
	task->state = STATE_ALIVE;

	task->lastpoll = time(0);
	pipe(task->pipe_fd); 
	fcntl(task->pipe_fd[0], O_NONBLOCK, &on); 
	fcntl(task->pipe_fd[1], O_NONBLOCK, &on); 

	if((task->pid=fork()) == 0) {
		int i;
		char *real_cmd;
		int len = strlen(cmd); 

		real_cmd = malloc(len + 64); 
		//free(task); /* we won't need it */
		/* ok we're now in the child process */
		/* nuke all fds */
		for(i=0; i < 1024; i++)
			if(i != task->pipe_fd[1])
				close(i); 
		/* not sure if this is really necessary */
		snprintf(real_cmd, len+64, "/bin/sh -c '%s'",  cmd); 

		open("/dev/null", O_RDONLY); /* stdin */
		dup2(task->pipe_fd[1], 1);
		open("/dev/zero", O_RDONLY); /* stderr */

		system(real_cmd); 
		exit(0); 
	}
	printf("task pid %d\n", task->pid); 
	task->id = rand();
	SLIST_INSERT(&task_list, &task->n); 

	return task;
}

int recv_msg(int sockfd, const struct msg *msg, int size, const struct sockaddr_in *raddr) {
	unsigned int siz = sizeof(*raddr);
	return recvfrom(sockfd, msg, size, 0, (struct sockaddr*)raddr, &siz);
}

int init_socket(void) {
	int sockfd;
	struct sockaddr_in saddr;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd == -1) {
		perror("socket");
		exit(-1);
	}
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //INADDR_ANY;
	saddr.sin_port = htons(SERVER_PORT); 
	if(bind(sockfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
		perror("bind");
		exit(-1);
	}
	return sockfd;
}

void send_data(const struct task *task, int sockfd, const struct sockaddr_in *raddr) {
	struct msg *msg;
	int len;
	char fil[256];

	msg = malloc(sizeof(struct msg) + MAX_CHUNK);
	memset(msg, 0, sizeof(*msg));
	msg->cmd = MSG_RESPONSE;
	msg->state = task->state; 

	if(task == NULL) {
		sendto(sockfd, msg, sizeof(*msg), 0,
		       (struct sockaddr*)raddr, sizeof(*raddr)); 
	} else {
		task->lastpoll = time(NULL);
		snprintf(fil, 256, AJAXER_DIR "/%d.out", task->pid);
		
		msg->more_to_follow = 1;
		while((len = read(task->pipe_fd[0], msg->data, MAX_CHUNK)) 
		      == MAX_CHUNK) {
			msg->data[MAX_CHUNK-1]=0;
			msg->size = len;
			sendto(sockfd, msg, sizeof(*msg)+len, 0,
			       (struct sockaddr*)raddr, sizeof(*raddr)); 
		}
		if(len <= 0) {
			msg->data[0]=0;
			msg->size=0;
			len = 0;
		} else {
			msg->data[len-1]=0;
			msg->size = len;
		}
		
		msg->more_to_follow = 0;
		sendto(sockfd, msg, sizeof(*msg)+len, 0,
		       (struct sockaddr*)raddr, sizeof(*raddr)); 
	}
}

int handle_timeout(void) {
	struct list_elem *le, *next;
	struct list_elem **p;
	struct task *task;
	pid_t pid; 
	char fil[256];
	int works = 0;

	/* FIXME: 
	 *   We need to safely iterate this list while removing elements
	 */
	p = &task_list;

	for(le = task_list; le != NULL; le=next) {		
		int status;

		next = le->next;
		task = container_of(le, struct task, n); 
		pid = task->pid;
		if((time(0)-task->lastpoll) > TIMEOUT) { 
			//struct list_elem *tmp;
			printf("Time to whack him\n");
			SLIST_REMOVE_ELEM(p, le); 
			snprintf(fil, 256, AJAXER_DIR "/%d.out", task->pid);
			/* Nobodys watching so clean out */
			kill(pid, SIGKILL);
			waitpid(task->pid, &status, WNOHANG); 
			/* free the memory */
			free(task);
		} else {
			p = &le->next;
			works++;

			if(waitpid(task->pid, &status, WNOHANG) == task->pid) {
				/* process went away */
				SLIST_REMOVE_ELEM(p, le);
				task->state = STATE_DEAD; 
			}
		}
	}
	//printf("out of loop\n");
	return works;
}

void * timer(void *arg) {
	int sockfd;
	struct sockaddr_in saddr;
	struct msg msg;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd == -1) {
		perror("socket");
		exit(-1);
	}
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
	saddr.sin_port = htons(SERVER_PORT); 
	if(connect(sockfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
		perror("connect");
		exit(-1);
	}
	
	memset(&msg, 0, sizeof(msg));
	msg.cmd = MSG_TIMEOUT;

	while(1) {
		sleep(1);
		send(sockfd, &msg, sizeof(msg), 0); 
	}
	return NULL;
}

int main(void) {
	int sockfd = init_socket();
	struct msg *msg;
	struct sockaddr_in raddr;
	pthread_t pt;
	int len;
	int fd = open("/dev/random", O_RDONLY);
	unsigned int seed; 
	//int works_active = 0;
	//struct list_elem *delayed_work = NULL;
	
	/* We should have a list of delayed work */

	while(read(fd, &seed, sizeof(unsigned int)) != 4);
	srand(seed); /* FIXME */
	close(fd);
	msg = malloc(MAX_CHUNK+ sizeof(struct msg));
	memset(msg, 0, 32);
	
	/* holy muppet we must change this to a select */
	printf("get ready!\n");
	pthread_create(&pt, NULL, timer, NULL);
	while(1) {
		len = recv_msg(sockfd, msg, MAX_CHUNK+sizeof(struct msg), &raddr);
		if(len < sizeof(struct msg))
			continue;
		if(len < msg->size)
			continue;
		if(len <= 0)
			break;
		switch(msg->cmd) {
		case MSG_EXEC:
		{
			struct task *task;
			printf("received an EXEC cmd\n");
			if(msg->size < (MAX_CHUNK)) {
				msg->data[msg->size]=0;
				task = spawn_task(msg->data);
				msg->id = task->id;
				msg->size = 0;
				msg->cmd = MSG_EXECD;
				sendto(sockfd, msg, sizeof(*msg), 0,
				       (struct sockaddr*)&raddr, sizeof(raddr)); 
			}
			break;
		}

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
				kill(pid, SIGKILL); 
			}
			break;
		}

		case MSG_TIMEOUT:
		{
			//works_active = 
			handle_timeout();
#if 0
			while(works < MAX_ACTIVE && !LIST_EMPTY(delayed_work)) {
				works++;
				SLIST_REMOVE_HEAD(delayed_work); 
				
			}
#endif
			break;
		}
		default:
			printf("uhm ajaxers received something we did not "
			       "expect.. %d %x\n ", msg->cmd, msg->cmd); 
			break;
		}
		memset(msg, 0, 32);       
	}

	return 0;
}
