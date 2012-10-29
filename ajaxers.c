#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include "list.h"
#include "protocol.h"

#define MAX_CHUNK 4096
#define AJAXER_DIR "/tmp/ajax"

#define MSG_TIMEOUT 12333
#define TIMEOUT 10


#define printd(lvl, fmt, args...) do { syslog(LOG_ERR, fmt, ##args); printf(fmt, ##args); } while(0);
struct task {
	struct list_elem n;
	pid_t pid;
	uint32_t id;
	int pipe_fd[2];
	//FILE *fp; 
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
	int flags;
	int i;

	printd(LVL_DBG, "try to spawn! [%s]\n", cmd);
	task = malloc(sizeof(*task));
	if(task == NULL)
		return NULL;
	task->state = STATE_ALIVE;

	task->lastpoll = time(0);
	pipe(task->pipe_fd); 

	flags = fcntl(task->pipe_fd[0], F_GETFL, 0);
	fcntl(task->pipe_fd[0], F_SETFL, flags|O_NONBLOCK);
	/* make the read side unbuffered */
	//task->fp = fdopen(task->pipe_fd[0], "r");
	//setvbuf(task->fp, NULL, _IONBF, 0); 

	flags = fcntl(task->pipe_fd[1], F_GETFL, 0);
	fcntl(task->pipe_fd[1], F_SETFL, flags|O_NONBLOCK);

	if((task->pid=fork()) == 0) {

		free(task); /* we won't need it */
		/* ok we're now in the child process */
		/* nuke all fds */
		for(i=0; i < 1024; i++)
			if(i != task->pipe_fd[1])
				close(i); 
		/* not sure if this is really necessary */

		open("/dev/zero", O_RDONLY); /* 0 = stdin */
		dup2(task->pipe_fd[1], 1);   /* 1 = stdout */
		dup2(task->pipe_fd[1], 2);   /* 2 = stderr */
		close(task->pipe_fd[1]); 

		//stdout = fdopen(1, "w");
		//stderr = fdopen(2, "w");

		system(cmd); 
		exit(0); 
	}
	printd(LVL_DBG, "task pid %d\n", task->pid); 
	while((task->id = rand()) == 0);
	SLIST_INSERT(&task_list, &task->n); 

	return task;
}

int recv_msg(int sockfd, struct msg *msg, int size, struct sockaddr *raddr) {
	unsigned int siz = sizeof(*raddr);
	return recvfrom(sockfd, msg, size, 0, raddr, &siz);
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

void send_data(struct task *task, int sockfd, const struct sockaddr *raddr) {
	struct msg *msg;
	int len;
	int status;

	msg = malloc(sizeof(struct msg) + MAX_CHUNK);
	if(msg == NULL)
		return;
	memset(msg, 0, sizeof(*msg));
	msg->cmd = MSG_RESPONSE;

	if(task == NULL) {
		msg->state = STATE_DONE; /* It might never have existed.. */
		sendto(sockfd, msg, sizeof(*msg), 0, raddr, sizeof(*raddr)); 
		return;
	}
	msg->state = task->state; 
	if(task->state == STATE_DONE) {
		printd(LVL_DBG, "Task is done!\n"); 
		sendto(sockfd, msg, sizeof(*msg), 0, raddr, sizeof(*raddr));
		/* At this point... we could clean up */
		return;
	}

	task->lastpoll = time(NULL);
	printd(LVL_DBG, "Increasing task %u timeout to %u\n", task->id, (unsigned int)task->lastpoll);
	
	msg->more_to_follow = 1;
	while((len = read(task->pipe_fd[0], msg->data, MAX_CHUNK-1)) > 0) {
	//while((len=fread(msg->data, 1, MAX_CHUNK-1, task->fp)) > 0) {
		/* FIXME: Handle errnos */		
		printd(LVL_DBG, "inner loop found %d bytes of data\n", len);
		msg->data[len]=0;
		printd(LVL_DBG, "data is: %s\n", msg->data);
		msg->size = len;
		sendto(sockfd, msg, sizeof(*msg)+len, 0,
		       raddr, sizeof(*raddr)); 
	}
	if(len <= 0) {
		msg->data[0]=0;
		msg->size=0;
		len = 0;
	} else {
		msg->data[len]=0;
		msg->size = len;
	}
	printd(LVL_DBG, "outer loop found %d bytes of data\n", len);	
	printd(LVL_DBG, "data is: %s\n", msg->data);
	msg->more_to_follow = 0;
	sendto(sockfd, msg, sizeof(*msg)+len, 0, raddr, sizeof(*raddr)); 

	if(task->state == STATE_DEAD) {
		/* If it's dead there won't be more data at this point */
		task->state = STATE_DONE;
	}
	if(waitpid(task->pid, &status, WNOHANG) == task->pid) {
		/* He terminated */
		printf("We determined he died\n"); 
		task->state = STATE_DEAD;
	}	
}

int handle_timeout(void) {
	struct list_elem *le, *next;
	struct list_elem **p;
	struct task *task;
	pid_t pid; 
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
			printd(LVL_DBG, "Time to whack him\n");
			SLIST_REMOVE_ELEM(p, le); 
			/* Nobodys watching so clean out */
			kill(pid, SIGKILL);
			printd(LVL_DBG, "wait for pid: %d\n", task->pid);
			waitpid(task->pid, &status, WNOHANG); 
			printd(LVL_DBG, "status: %d\n", status); 
			/* free the memory and fds */
			close(task->pipe_fd[0]);
			close(task->pipe_fd[1]);
			//fclose(task->fp); 
			free(task);
		} else {
			p = &le->next;
			works++;

			if(waitpid(task->pid, &status, WNOHANG) == task->pid) {
				/* process went away, don't remove the task 
				 * pointer. If you do we might lose data
				 */
				task->state = STATE_DEAD; 
			}
		}
	}
	//printd(LVL_DBG, "out of loop\n");
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

int main(int argc, char **argv) {
	int sockfd = init_socket();
	struct msg *msg;
	struct sockaddr_in raddr;
	pthread_t pt;
	int len;
	int fd = open("/dev/random", O_RDONLY);
	unsigned int seed; 
	printf("%s\n", argv[0]); 
	//int works_active = 0;
	//struct list_elem *delayed_work = NULL;
	
	/* We should have a list of delayed work */

	//printf("Try to get random data\n"); 
	//while(read(fd, &seed, sizeof(unsigned int)) != 4);
	seed = time(0); 
	srand(seed); /* FIXME */
	close(fd);
	msg = malloc(MAX_CHUNK+ sizeof(struct msg));
	if(msg == NULL) {
		printd(LVL_DBG, "Unable to allocate memory, goodbye\n");
		return 1;
	}
	memset(msg, 0, 32);
	
	/* holy muppet we must change this to a select */
	printd(LVL_DBG, "get ready!\n");
	pthread_create(&pt, NULL, timer, NULL);
	while(1) {
		len = recv_msg(sockfd, msg, MAX_CHUNK+sizeof(struct msg), 
			       (struct sockaddr*)&raddr);
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
			printd(LVL_DBG,  "received an EXEC cmd\n");
			if(msg->size < (MAX_CHUNK)) {
				msg->data[msg->size] = 0;
				task = spawn_task(msg->data);
				if(task == NULL) {
					msg->id = -1;
				} else {
					msg->id = task->id;
				}
				msg->size = 0;
				msg->cmd = MSG_EXECD;
				printd(LVL_DBG,  "responding id: %u\n", msg->id);
				sendto(sockfd, msg, sizeof(*msg), 0,
				       (struct sockaddr*)&raddr, sizeof(raddr)); 
			}
			break;
		}

		case MSG_GET: 
		{
			struct task *task = lookup(msg->id);
			printd(LVL_DBG, "received a GET cmd task: %p\n", task);
			if(task != NULL) {
			    printd(LVL_DBG, "task id: %u\n", task->id); 
			}
			send_data(task, sockfd, (struct sockaddr*)&raddr); 
				
			break;
		}

		case MSG_KILL:
		{
			pid_t pid = lookup_pid(msg->id);
			int status;
			printd(LVL_DBG, "received a KILL cmd\n");
			if(pid != -1) {
				kill(pid, SIGKILL); 
				waitpid(pid, &status, WNOHANG);
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
			printd(LVL_DBG, "uhm ajaxers received something we did not "
			       "expect.. %d %x\n ", msg->cmd, msg->cmd); 
			break;
		}
		memset(msg, 0, 32);       
	}

	return 0;
}
