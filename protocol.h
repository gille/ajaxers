#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

struct msg {
	int cmd;
	uint32_t size;
	int more_to_follow;
	uint32_t id;
	char data[1];
};

#define MSG_EXEC 1
#define MSG_GET  2
#define MSG_KILL 3

#define SERVER_PORT 9999

#endif
