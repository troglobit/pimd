/* callout queue implementation
 *
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted". Use of the mrouted program represents acceptance
 * of the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */

#include "defs.h"

static struct tmr *Q = NULL;
static int id = 0;

struct tmr {
    struct tmr      *next;		/* next event */
    int        	     id;  
    cfunc_t          func;    	        /* function to call */
    void	    *data;		/* func's data */
    int              time;		/* time offset to next event*/
};

static void print_Q(void);

/* Get next free (non-zero) ID
 *
 * TODO: Refactor pimd to use uint32_t for ID.
 *       For now, make sure timer ID is never <= 0
 *
 * ID is a counter that wraps to zero, which is reserved.
 * The range of counters IDs is big so we should be OK.
 */
static int next_id(void)
{
    id++;
    if (id <= 0)
	id = 1;

    return id;
}

void timer_init(void)
{
    Q = NULL;
    id = 0;
}

void timer_exit(void)
{
    struct tmr *p;
    
    while (Q) {
	p = Q;
	Q = Q->next;
	if (p->data)
	    free(p->data);
	free(p);
    }

    timer_init();
}

/*
 * elapsed_time seconds have passed; perform all the events that should
 * happen.
 */
void timer_age_queue(int elapsed_time)
{
    struct tmr *ptr;

    IF_DEBUG(DEBUG_TIMEOUT)
	logit(LOG_DEBUG, 0, "aging queue (elapsed time %d):", elapsed_time);
    print_Q();
    
    for (ptr = Q; Q; ptr = Q) {
	if (ptr->time  > elapsed_time) {
	    ptr->time -= elapsed_time;
	    break;
	}

	/* ptr has expired, push Q */
	Q             = ptr->next;
	elapsed_time -= ptr->time;

	if (ptr->func)
	    ptr->func(ptr->data);
	free(ptr);
    }
}

/*
 * Return in how many seconds timer_age_queue() would like to be called.
 * Return -1 if there are no events pending.
 */
int timer_next_delay(void)
{
    if (!Q)
	return -1;

    if (Q->time < 0) {
	logit(LOG_WARNING, 0, "%s(): top of queue says %d", __func__, Q->time);
	return 0;
    }

    return Q->time;
}

/* 
 * Create a timer
 * @delay: Number of seconds for timeout
 * @action: Timer callback
 * @data: Optional callback data, must be a dynically allocated ptr
 */
int timer_set(int delay, cfunc_t action, void *data)
{
    struct tmr *ptr, *node, *prev;
    
    IF_DEBUG(DEBUG_TIMEOUT)
	logit(LOG_DEBUG, 0, "setting timer:");
    print_Q();
    
    /* create a node */	
    node = calloc(1, sizeof(struct tmr));
    if (!node) {
	logit(LOG_ERR, 0, "Ran out of memory in %s()", __func__);
	return -1;
    }

    node->func = action; 
    node->data = data;
    node->time = delay; 
    node->next = 0;	
    node->id   = next_id();
    
    prev = ptr = Q;
    
    /* insert node in the queue */
    
    /* if the queue is empty, insert the node and return */
    if (!Q)
	Q = node;
    else {
	/* chase the pointer looking for the right place */
	while (ptr) {
	    if (delay < ptr->time) {
		/* right place */
		node->next = ptr;
		if (ptr == Q)
		    Q = node;
		else
		    prev->next = node;
		ptr->time -= node->time;
		print_Q();

		return node->id;
	    }

	    /* keep moving */
	    delay -= ptr->time;
	    node->time = delay;
	    prev = ptr;
	    ptr = ptr->next;
	}
	prev->next = node;
    }
    print_Q();

    return node->id;
}

/* returns the time until the timer is scheduled */
int timer_get(int timer_id)
{
    struct tmr *ptr;
    int left = 0;
	
    if (!timer_id)
	return -1;
    
    for (ptr = Q; ptr; ptr = ptr->next) {
	left += ptr->time;
	if (ptr->id == timer_id)
	    return left;
    }

    return -1;
}

/* clears the associated timer */
void timer_clear(int timer_id)
{
    struct tmr  *ptr, *prev;
    
    if (!timer_id)
	return;
    
    prev = ptr = Q;
    
    /*
     * find the right node, delete it. the subsequent node's time
     * gets bumped up
     */
    
    print_Q();
    while (ptr) {
	if (ptr->id != timer_id) {
	    prev = ptr;
	    ptr = ptr->next;
	    continue;
	}

	/* Found it, now unlink it from the queue */
	if (ptr == Q)
	    Q = ptr->next;
	else
	    prev->next = ptr->next;
	    
	/* increment next node if any */
	if (ptr->next)
	    (ptr->next)->time += ptr->time;
	    
	if (ptr->data)
	    free(ptr->data);
	free(ptr);
	break;
    }
    print_Q();
}

/*
 * debugging utility
 */
static void print_Q(void)
{
    struct tmr  *ptr;
    
    IF_DEBUG(DEBUG_TIMEOUT) {
	for (ptr = Q; ptr; ptr = ptr->next)
	    logit(LOG_DEBUG, 0, "(%d,%d) ", ptr->id, ptr->time);
    }
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "cc-mode"
 * End:
 */
