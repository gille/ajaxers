#include <stdio.h>
#include <sys/socket.h>

#define MAX_CHUNK 10000

struct task {
	pid_t pid;
	
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
	pid_t pid;
	task = malloc(sizeof(task));
	if((task->pid=fork()) == 0) {
		char *real_cmd;
		char pipe_to[64];
		free(task); /* we won't need it */
		sprintf("/tmp/ajax/%s.out", getpid()); 
		/* ok we're now in the child process */
		/* nuke all fds */
		for(i=0; i < 1024; i++)
			close(i); 
		system(cmd); 
	}
}

int main(void) {
	int sockfd = socket(AF_INET);
	struct msg *msg;
	
	msg = malloc();
	memset(msg, 0, 32);
	recvmsg();
	
	
	switch(msg->cmd) {
	case MSG_CMD:
		spawn_task(msg->data);
		break;
	case MSG_GET:
		struct task = lookup(msg->id);
	case MSG_KILL:
		pid_t pid = lookup_pid(msg->id);
		if(pid != -1) {
		}
		break;
	default:
		printf("uhm ajaxers received something we did not "
		       "expect.. %d %x\n ", msg->cmd, msg->cmd); 
		break;
	}

}
