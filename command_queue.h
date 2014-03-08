#ifndef __command_queue_h__
#define __command_queue_h__

#include <inttypes.h>

typedef struct QNode
{
    struct QNode* next;
    uint8_t cmd;
}QNode_t;

typedef struct command_queue {
	QNode_t* head;
    QNode_t* tail;
	int	size;
}  command_queue_t;

/* NEED TEST */

/* ------------------ prototypes -- */
void command_queue_init(command_queue_t *q);
int command_queue_empty(command_queue_t *q);
void command_queue_enqueue(command_queue_t *q, uint8_t cmd);
uint8_t command_queue_dequeue(command_queue_t *q);
uint8_t command_queue_head(command_queue_t* q);
void free_queue(command_queue_t* q);

#endif