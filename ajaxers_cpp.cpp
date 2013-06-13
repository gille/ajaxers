#include <vector>
#include <map>
#include <iostream>
#include <assert.h>

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

using namespace std;

#define printd(lvl, fmt, args...) printf(fmt, ##args)

#define MAX_CHUNK 4096
#define FD_MAX 1024

/* 
   cmd 
   @more_to_follow only used when the server sends data back to client indicating data is split into multiple buffers.
   @id 
*/
struct msg {
	int cmd;
	uint32_t size;
	int more_to_follow;
	uint32_t id;
	int state;
	char data[1];
};

#define MSG_EXEC       1
#define MSG_EXECD      2
#define MSG_GET        3
#define MSG_RESPONSE   4
#define MSG_KILL       5

#if 0
#define STATE_ALIVE    0
#define STATE_DEAD     1
#define STATE_DONE     2
#endif

#define SERVER_PORT 9999


enum state {
	STATE_ALIVE = 1,
	STATE_DEAD,
	STATE_DONE
};

class task {
private:
	int timeout;
	uint32_t task_id; 
	pid_t task_pid;
	enum state state; 
	int pipe_fd[2];
public:
	task(char *cmd) {
		int flags;
		int i;

		timeout = 10;
		state = STATE_ALIVE;
		pipe(pipe_fd); 		
		printd(LVL_DBG, "Pipe spawned: %d %d\n", pipe_fd[0], pipe_fd[1]);
		flags = fcntl(pipe_fd[0], F_GETFL, 0);
		fcntl(pipe_fd[0], F_SETFL, flags|O_NONBLOCK);
		flags = fcntl(pipe_fd[1], F_GETFL, 0);
		fcntl(pipe_fd[1], F_SETFL, flags|O_NONBLOCK);
		while((task_id = rand()) == 0);
		
		printd(LVL_DBG, "Spawning task: [%s]\n", cmd);
		if((task_pid=fork()) == 0) {
			/* ok we're now in the child process */
			/* nuke all fds */
			close(0);
			close(1);
			close(2);
			open("/dev/zero", O_RDONLY); /* 0 = stdin */
			dup2(pipe_fd[1], 1);   /* 1 = stdout */
			open("/dev/null", O_RDONLY); /* 2 = stderr */
			for(i=3; i < FD_MAX; i++)
				close(i); 
			system(cmd); 
			exit(0); 
		}
		printd(LVL_DBG, "task pid %d\n", task_pid);
	}

	uint32_t id() {
		return this->task_id;
	}

	pid_t pid() {
		return this->task_pid;
	}

	enum state get_state() {
		return this->state; 
	}

	void send_data(int sockfd, struct sockaddr *saddr) {
		struct msg *msg = (struct msg*)malloc(sizeof(*msg) + MAX_CHUNK);
		ssize_t len;

		if(msg == NULL)
			return;
		memset(msg, 0, sizeof(*msg) + MAX_CHUNK);
		msg->cmd = MSG_RESPONSE;
				
		msg->state = state; 

		/* A dead task might still have data in the pipe for us 
		 */
		if(state == STATE_ALIVE || state == STATE_DEAD) {
			printd(LVL_DBG, "We're good so send away!\n");
			if(state == STATE_ALIVE)
				timeout = 10;

			msg->more_to_follow = 1;
			while(msg->more_to_follow) {
				printd(LVL_DBG, "reading from pipe: %d\n", pipe_fd[0]);
				len = read(pipe_fd[0], msg->data, MAX_CHUNK-1);
				printd(LVL_DBG, "Got: %d\n", len);
				if(len <= 0) {
					if(len == -1) {
						if(!(errno == EWOULDBLOCK || errno == EAGAIN ||
						     errno == EINTR))
						{
							printd(LVL_DBG, "Oops something bad happened to our FD\n");
							/* There's no point to pass through dead() since we'll call done() below anyway */
							done();
						}
					}
					//printd(LVL_DBG, "Oops OOD\n");
					msg->more_to_follow = 0;
					msg->data[0]=0;
					msg->size=0;
					len = 0;
				}
				if(len < (MAX_CHUNK-1))
					msg->more_to_follow = 0;
				printd(LVL_DBG, "inner loop found %d bytes of data\n", len);
				msg->data[len]=0;
				msg->size = len;
				/* fixme: remove the unnecessary 0 len send */
				sendto(sockfd, msg, sizeof(*msg)+len, 0,
				       saddr, sizeof(*saddr)); 
			}
			if(state == STATE_DEAD) {
				/* If it's dead there won't be more data at this point */
				this->done();
			}
		} else {
			/* We must be around, but not quite happy */
			sendto(sockfd, msg, sizeof(*msg), 0, saddr, sizeof(*saddr)); 
		}	       
		free(msg);
	}

	void tick() {
		/* Fixme: carry this as a class member? 
		 */
		printd(LVL_DBG, "ticking id: %u...\n", id());
		timeout--;
		if(timeout <= 0) {
			switch(state) {
			case STATE_ALIVE:
				printd(LVL_DBG, "[%d] Alive => Dead\n", id());
				state = STATE_DEAD;
				break;			       
			case STATE_DEAD:
				printd(LVL_DBG, "[%d] Dead => Done\n", id());
				state = STATE_DONE;
				break;
			default:
				printd(LVL_DBG, "[%d] tick...tock... %d => Done\n", id(), timeout);
				break;
			}
		}
	}

	void done() {
		int status; 
		printd(LVL_DBG, "[%d] => Done\n", id());
		this->state = STATE_DONE;
		waitpid(task_pid, &status, WNOHANG);
		/* We will never read again, so just close the pipes */
		close(pipe_fd[0]);
		close(pipe_fd[1]);
	};

	void dead() {
		printd(LVL_DBG, "[%d] => Dead\n", id());
		this->state = STATE_DEAD;
	}
	void kill(void) {
		int status;
		::kill(task_pid, SIGKILL); 
		waitpid(task_pid, &status, WNOHANG);
	}
	void finalize(void) {
#if 0
		int status;
		::kill(task_pid, SIGKILL); 
		waitpid(task_pid, &status, WNOHANG);
#endif
		/* FIXME: he's already dead right? */
	}
};

class scheduler { 
private:
	vector<task*> tasks;
	map<uint32_t, task*> id_map;
	map<pid_t, task*> pid_map;
	time_t old_time;

	task* find_task(uint32_t id) {
		auto it  = id_map.find(id);
		if(it == id_map.end())
			return NULL;
		return (*it).second;
	}

	task* find_task(pid_t pid) {
		auto it = pid_map.find(pid);
		if(it == pid_map.end())
			return NULL;
		return (*it).second;
	}

public:
	scheduler() {
		old_time = time(0);
	}

	void send_data(int sockfd, struct sockaddr *saddr, uint32_t id) {
		task *t = find_task(id);		
		printf("[sch] find data found: %p\n", t); 
		if(t)
			t->send_data(sockfd, saddr); 
		else {
			struct msg *msg = (struct msg*)malloc(sizeof(*msg) + MAX_CHUNK);
			if(msg == NULL)
				return;
			memset(msg, 0, sizeof(*msg) + MAX_CHUNK);
			msg->cmd = MSG_RESPONSE;
			
			msg->state = STATE_DONE; /* It might never have existed.. */
			sendto(sockfd, msg, sizeof(*msg), 0, saddr, sizeof(*saddr)); 
			free(msg);
		}	

	}

	int spawn_task(char *cmd) {
		class task *t = new task(cmd); 
	
		tasks.push_back(t);
		/* these maps aren't O(1), O(log N) is still good enough for us */
		id_map.insert(std::pair<uint32_t, task* >(t->id(), t));
		pid_map.insert(std::pair<pid_t, task*>(t->pid(), t)); 
		return t->id();
	}

	void tick() {
		time_t t = time(0); 
		pid_t pid;
		int status;

		while(old_time != t) {
			for(auto it = tasks.begin(); it != tasks.end(); it++) {
				if((*it)->get_state() == STATE_DONE) {
					task *t; 
					(*it)->finalize(); 
					id_map.erase((*it)->id());
					pid_map.erase((*it)->pid());
					assert(find_task((*it)->id()) == NULL);
					assert(find_task((*it)->pid()) == NULL);
					/* After this the iterator is invalid, so do this
					 * last 
					 */
					t = *it;
					it = tasks.erase(it); 
					it--;
					delete (t);
					continue;
					/* Done */
				} else {				
					(*it)->tick();
				}
			}
			while((pid=waitpid(-1, &status, WNOHANG)) > 0) {
				/* Ok, a child has died */
				/* By definition this should NEVER be NULL */
				task *t = find_task(pid);
				if(t) 
					t->dead(); 
			}
			old_time++;
		}
	}
	
	void kill(uint32_t id) {
		task *t = find_task(id);
		if(t) 
			t->kill();
	}
};

int init_socket(void) {
	int sockfd;
	struct sockaddr_in saddr;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd == -1) {
		perror("socket");
		exit(-1);
	}
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	saddr.sin_port = htons(SERVER_PORT); 
	if(bind(sockfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
		perror("bind");
		exit(-1);
	}
	return sockfd;
}

int main(int argc, char **argv) {
	int sockfd = init_socket();
	struct msg *msg;
	struct sockaddr_in raddr;
	size_t len;
	unsigned int seed; 
	struct timeval tv;
	int fds;
	fd_set r;
	scheduler s;
	socklen_t addrlen;

	tv.tv_sec = 1; 
	tv.tv_usec = 0;
	seed = time(0); 
	srand(seed); /* FIXME */

	msg = (struct msg*)malloc(MAX_CHUNK+ sizeof(*msg));
	if(msg == NULL) {
		printd(LVL_DBG, "Unable to allocate memory, goodbye\n");
		return 1;
	}
	memset(msg, 0, sizeof(*msg));
	
	FD_ZERO(&r);
	FD_SET(sockfd, &r);
	printd(LVL_DBG, "Ready to serve my master\n");
	while(1) {
		/* we only re-initialize the timer when it has timed out */
		fds = select(sockfd+1, &r, NULL, NULL, &tv); 
		if(fds == 0) {
			/* no socket ==> timeout */
			tv.tv_sec = 1; 
			tv.tv_usec = 0;
			FD_ZERO(&r);
			FD_SET(sockfd, &r);

			s.tick();
			continue;
		}
		addrlen = sizeof(struct sockaddr);
		len = recvfrom(sockfd, msg, MAX_CHUNK+sizeof(*msg), 0,
			       (struct sockaddr*)&raddr, &addrlen);
		if(len <= 0) /* we lost the socket */
			break;
		if(len < sizeof(*msg) || len < msg->size)
			continue;
		
		switch(msg->cmd) {
		case MSG_EXEC:
		{		    
			int id;
			printd(LVL_DBG,  "received an EXEC cmd %s\n", msg->data);
			if(msg->size < (MAX_CHUNK)) {
				msg->data[msg->size] = 0;
				id = s.spawn_task(msg->data);
				if(id == 0)
					msg->id = -1;
				else
					msg->id = id;
				
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
			s.send_data(sockfd, (struct sockaddr*)&raddr, msg->id); 
			break;
		}

		case MSG_KILL:
		{
			s.kill(msg->id); 
			break;
		}
		default:
			printd(LVL_DBG, "uhm ajaxers received something we did not "
			       "expect.. %d %x\n ", msg->cmd, msg->cmd); 
			break;
		}
		memset(msg, 0, sizeof(*msg));
	}
	free(msg);
	return 0;
}

