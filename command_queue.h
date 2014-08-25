/*
 Header file: command_queue.h
 Brief: The prototypes for the command queue
 Author: yanli (yan_li@brown.edu)
 Time stamp: 03/20/2014
 */

#ifndef __COMMAMD_QUEUE_H__
#define __COMMAND_QUEUE_H__

#include <inttypes.h>

typedef struct qnode
{
    struct qnode* next;
    uint8_t cmd;
}qnode_t;

typedef struct command_queue {
	qnode_t* head;
    qnode_t* tail;
	int	size;
}  command_queue_t;

/* --------------------------- prototypes ----------------------------------- */
void command_queue_init(command_queue_t *q);
int command_queue_empty(const command_queue_t *q);
void command_queue_enqueue(command_queue_t *q, uint8_t cmd);
uint8_t command_queue_dequeue(command_queue_t *q);
uint8_t command_queue_head(const command_queue_t* q);
void free_queue(command_queue_t* q);

#endif