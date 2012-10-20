#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <syslog.h>
#include <string.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

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

	time_t lastpoll;
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

	printf("try to spawn! [%s]\n", cmd);
	task = malloc(sizeof(*task));

	task->lastpoll = time(0);

	if((task->pid=fork()) == 0) {
		int i;
		char *real_cmd;
		int len = strlen(cmd); 
		real_cmd = malloc(len + 64); 
		//free(task); /* we won't need it */
		/* ok we're now in the child process */
		/* nuke all fds */
		for(i=0; i < 1024; i++)
			close(i); 

		snprintf(real_cmd, len+64, "/bin/sh -c '%s' > /tmp/ajax/%d.out",  
			 cmd, getpid()); 
#if 0
		open("/dev/null", O_RDONLY); /* stdin */
		open(real_cmd, O_CREAT|O_WRONLY); /*?*/
		open("/dev/zero", O_RDONLY); /* stdin */
#endif
		system(real_cmd); 
		exit(0); 
	}
	printf("task pid %d\n", task->pid); 
	task->id = rand();
	SLIST_INSERT(&task_list, &task->n); 
	
	return task;
}

int recv_msg(int sockfd, struct msg *msg, int size, struct sockaddr_in *raddr) {
	unsigned int siz = sizeof(*raddr);
	return recvfrom(sockfd, msg, size, 0, (struct sockaddr*)raddr, &siz);
}

int init_socket() {
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

void send_data(struct task *task, int sockfd, struct sockaddr_in *raddr) {
	struct msg *msg;
	int len; 
	int fd;
	char fil[256];
	msg = malloc(sizeof(struct msg) + MAX_CHUNK);
	memset(msg, 0, sizeof(*msg));
	msg->cmd = MSG_RESPONSE;
	
	if(task == NULL) {
		sendto(sockfd, msg, sizeof(*msg), 0,
		       (struct sockaddr*)raddr, sizeof(*raddr)); 
	} else {
		task->lastpoll = time(NULL);
		snprintf(fil, 256, AJAXER_DIR "/%d.out", task->pid);
		printf("got pid %d try to open %s\n", task->pid, fil); 
		fd = open(fil, O_RDONLY);
		if(fd == -1) {
			perror("open"); 
			sendto(sockfd, msg, sizeof(*msg), 0,
			       (struct sockaddr*)raddr, sizeof(*raddr)); 
		} else {
			msg->more_to_follow = 1;
			while((len = read(fd, msg->data, MAX_CHUNK)) == MAX_CHUNK) {
				msg->data[MAX_CHUNK-1]=0;
				msg->size = len;
				sendto(sockfd, msg, sizeof(*msg)+len, 0,
				       (struct sockaddr*)raddr, sizeof(*raddr)); 
			}
			msg->size = len;
			msg->more_to_follow = 0;
			sendto(sockfd, msg, sizeof(*msg)+len, 0,
			       (struct sockaddr*)raddr, sizeof(*raddr)); 

			close(fd);
		}
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
		next = le->next;
		task = container_of(le, struct task, n); 
		pid = task->pid;
		if((time(0)->lastpoll) > TIMEOUT) { 
			//struct list_elem *tmp;
			printf("Time to whack him\n");
			SLIST_REMOVE_ELEM(p, le); 
			snprintf(fil, 256, AJAXER_DIR "/%d.out", task->pid);
			/* Nobodys watching so clean out */
			kill(pid, SIGKILL);
			unlink(fil);
			/* free the memory */
			free(task);
		} else {
			p = &le->next;
			works++;
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
	int works_active = 0;
	struct list_elem *delayed_work = NULL;
	
	/* We should have a list of delayed work */

	read(fd, &seed, sizeof(unsigned int));
	srand(seed); /* FIXME */

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
			works_active = handle_timeout();
			while(works < MAX_ACTIVE && !LIST_EMPTY(delayed_work)) {
				works++;
				SLIST_REMOVE_HEAD(delayed_work); 
				
			}
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
