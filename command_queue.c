#include "command_queue.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
void command_queue_init(command_queue_t *q)
{
    assert(q != NULL);
    memset(q, 0, sizeof(command_queue_t));
}

int command_queue_empty(command_queue_t *q)
{
    assert(q != NULL);
    return q->size == 0 && q->head == q->tail && q->head == NULL;
}

void command_queue_enqueue(command_queue_t* q, uint8_t cmd)
{
    if(command_queue_empty(q))
    {
        q->head = (QNode_t*)malloc(sizeof(QNode_t));
        memset(q->head, 0, sizeof(QNode_t));
        if(q->head == NULL)
        {
            fprintf(stderr, "Malloc failed.\n");
        }
        q->head->cmd = cmd;
        q->tail = q->head;
        q->size++;
    }else
    {
        q->tail->next = (QNode_t*)malloc(sizeof(QNode_t));
        memset(q->tail->next, 0, sizeof(QNode_t));
        if(q->tail->next == NULL)
        {
            fprintf(stderr, "Malloc failed.\n");
        }
        q->tail->next->cmd = cmd;
        q->tail = q->tail->next;
        q->size++;
    }
}

uint8_t command_queue_dequeue(command_queue_t *q)
{
    if(command_queue_empty(q))
    {
        fprintf(stderr, "Error: tried to dequeue from an empty queue.\n");
        return -1;
    }
    else
    {
        QNode_t* head = q->head;
        q->head = q->head->next;
        
        uint8_t cmd = head->cmd;
        free(head);
        q->size--;
        if(q->head == NULL)
            q->tail = NULL;
        return cmd;
    }
    
}

uint8_t command_queue_head(command_queue_t* q)
{
    if(command_queue_empty(q))
    {
        fprintf(stderr, "Error: tried to get head from an empty queue.\n");
        return -1;
    }else
    {
        return q->head->cmd;
    }
}

void free_queue(command_queue_t* q)
{
    while(!command_queue_empty(q))
    {
        QNode_t* next = q->head->next;
        free(q->head);
        q->head = next;
        if(q->head == NULL)
            q->tail = NULL;
        q->size--;
    }
}