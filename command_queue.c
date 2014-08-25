/*
 Source file: command_queue.c
 Brief: Implemented the functionality of the command queue
 Author: yanli (yan_li@brown.edu)
 Time stamp: 03/20/2014
 */

#include "command_queue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/*
 command_queue_init: initialize the queue
 @param *q: the pointer to the queue object
 */
void command_queue_init(command_queue_t *q)
{
    assert(q != NULL);
    memset(q, 0, sizeof(command_queue_t));
}

/*
 command_queue_empty: initialize the queue
 @return 1 for empty and o for non empty
 @param *q: the pointer to the queue object
 */
int command_queue_empty(const command_queue_t *q)
{
    assert(q != NULL);
    return q->size == 0 && q->head == q->tail && q->head == NULL;
}

/*
 command_queue_enqueue: enqueue a command
 @param *q: the pointer to the queue object
 @param cmd: the new command
 */
void command_queue_enqueue(command_queue_t* q, uint8_t cmd)
{
    if (command_queue_empty(q))
    {
        q->head = (qnode_t*)malloc(sizeof(qnode_t));
        memset(q->head, 0, sizeof(qnode_t));
        if (q->head == NULL)
        {
            fprintf(stderr, "Malloc failed.\n");
        }
        q->head->cmd = cmd;
        q->tail = q->head;
        q->size++;
    }else
    {
        q->tail->next = (qnode_t*)malloc(sizeof(qnode_t));
        memset(q->tail->next, 0, sizeof(qnode_t));
        if (q->tail->next == NULL)
        {
            fprintf(stderr, "Malloc failed.\n");
        }
        q->tail->next->cmd = cmd;
        q->tail = q->tail->next;
        q->size++;
    }
}

/*
 command_queue_dequeue: dequeue one item from the queue
 @return: -1 for exception and in other cases just return the head command
 @param *q: the pointer to the command queue
 */
uint8_t command_queue_dequeue(command_queue_t *q)
{
    if (command_queue_empty(q))
    {
        fprintf(stderr, "Error: tried to dequeue from an empty queue.\n");
        return -1;
    }
    else
    {
        qnode_t* head = q->head;
        q->head = q->head->next;
        
        uint8_t cmd = head->cmd;
        free(head);
        q->size--;
        if (q->head == NULL)
            q->tail = NULL;
        return cmd;
    }
}

/*
 command_queue_head: return the head of the command queue
 @return: -1 for exception and in other cases just return the head command
 @param *q: the pointer to the queue object
 */
uint8_t command_queue_head(const command_queue_t* q)
{
    if (command_queue_empty(q))
    {
        fprintf(stderr, "Error: tried to get head from an empty queue.\n");
        return -1;
    }else
        return q->head->cmd;
}

/*
 free_queue: free the command queue
 @param *q: the pointer to the queue
 */
void free_queue(command_queue_t* q)
{
    while (!command_queue_empty(q))
    {
        qnode_t* next = q->head->next;
        free(q->head);
        q->head = next;
        if (q->head == NULL)
            q->tail = NULL;
        q->size--;
    }
}