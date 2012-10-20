#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

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

#define STATE_ALIVE    0
#define STATE_DEAD     1

#define SERVER_PORT 9999

#endif
