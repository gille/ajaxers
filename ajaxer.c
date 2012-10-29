#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>

#include <netinet/in.h>

#include "protocol.h"

#define MAX_CHUNK 10000

#define printd(fmt, args...) 

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
	if(connect(sockfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
		perror("connect");
		exit(-1);
	}
	return sockfd;
}

void usage(const char *name) {
	printf("%s usage:\n"
	       "%s [-e cmd]|[-g id]|[-k id]\n", name, name);
	exit(-1);
}

void htmlize(const char *str) {
	while(*str != 0) {
		if(*str == '\n')
			printf("<br />");
		printf("%c", *str); 
		str++;
	}
}

int main(int argc, char **argv) {
	int sockfd = init_socket();
	struct msg *msg;
	int i;
	int ret;
	int len;
	int cmd; 
	struct timeval tv; 
	fd_set rset;

	if(argc < 3) {
		usage(argv[0]);
	}

	msg = malloc(MAX_CHUNK);
	memset(msg, 0, sizeof(*msg)); 
	
	len = sizeof(*msg);
	if(strcmp(argv[1], "-e") == 0 ) {
		cmd = MSG_EXEC;
		for(i=2; i < argc; i++) {
			strncat(msg->data, argv[i], MAX_CHUNK);
			strncat(msg->data, " ", MAX_CHUNK); 
		}
		msg->size = strlen(msg->data)+1;
		len = strlen(msg->data)+sizeof(*msg); 
	} else 	if(strcmp(argv[1], "-g") == 0 ) {
		cmd = MSG_GET;
		msg->id = atoi(argv[2]);
	} else 	if(strcmp(argv[1], "-k") == 0 ) {
		cmd = MSG_KILL;
		/* FIXME: better conversion */
		msg->id = atoi(argv[2]);
	} else
	{
		usage(argv[0]);
	}
	msg->data[MAX_CHUNK-1]=0;

	msg->cmd = cmd;
	ret = send(sockfd, msg, len, 0); 
	if(ret != len) {
		perror("send");
		exit(-1); 
	}
	FD_ZERO(&rset);
	FD_SET(sockfd, &rset); 
	tv.tv_usec = 0;
	tv.tv_sec = 3; 
	if(select(sockfd+1, &rset, NULL, NULL, &tv) == 1) {
		/* Interesting, if we send and nobody listens we get it back? 
		 */
		recv(sockfd, msg, MAX_CHUNK, 0);
		
		switch(msg->cmd) {
		case MSG_EXECD:
			printf("ajaxer_work_spawn=%d\n", msg->id); 
			break;
			
		case MSG_RESPONSE: 
			while(msg->more_to_follow) {
#if 0
				if(msg->size != strlen(msg->data)) {
					printf("error: size unmatched\n");
				}
#endif
				if(msg->size < MAX_CHUNK)
					printf("%s", msg->data); 
				/* FIXME: we should validate that the length isn't broken */
				if(recv(sockfd, msg, MAX_CHUNK, 0) == -1)
					return 0;
			}
			printf("%s", msg->data); 
			if(msg->state == STATE_DONE)
			    return 1;
			break;
		default:
			/* If there's no server we'll get the question sent to us */
			//printf("Don't know what you're talking about.. %d\n", msg->cmd);
			break;
		}
	}
	/* out with no data */
	return 0;
}
