/* ====================================================================
 * The Kannel Software License, Version 1.0
 *
 * Copyright (c) 2001-2013 Kannel Group
 * Copyright (c) 1998-2001 WapIT Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Kannel Group (http://www.kannel.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Kannel" and "Kannel Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please
 *    contact org@kannel.org.
 *
 * 5. Products derived from this software may not be called "Kannel",
 *    nor may "Kannel" appear in their name, without prior written
 *    permission of the Kannel Group.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Kannel Group.  For more information on
 * the Kannel Group, please see <http://www.kannel.org/>.
 *
 * Portions of this software are based upon software originally written at
 * WapIT Ltd., Helsinki, Finland for the Kannel project.
 */

/*
 * gw-timer.c - timers and set of timers.
 *
 * See gw-timer.h for a description of the interface.
 */

#include <signal.h>

#include "gwlib/gwlib.h"
#include "gw-timer.h"

/*
 * Active timers are stored in a TimerHeap.  It is a partially ordered
 * array.  Each element i is the child of element i/2 (rounded down),
 * and a child never elapses before its parent.  The result is that
 * element 0, the top of the heap, is always the first timer to
 * elapse.  The heap is kept in this partial order by all operations on
 * it.  Maintaining a partial order is much cheaper than maintaining
 * a sorted list.
 * The array will be resized as needed.  The size field is the number
 * of elements for which space is reserved, and the len field is the
 * number of elements actually used.  The elements used will always be
 * at tab[0] through tab[len-1].
 */
struct TimerHeap
{
    Timer **tab;
    long len;
    long size;
};
typedef struct TimerHeap TimerHeap;

struct Timerset
{
    /*
     * This field is set to true when the timer thread should shut down.
     */
    volatile sig_atomic_t stopping;
    /*
     * The entire set is locked for any operation on it.  This is
     * not as expensive as it sounds because usually each set is
     * used by one caller thread and one (internal) timer thread,
     * and the timer thread does not wake up very often.
     */
    Mutex *mutex;
    /*
     * Active timers are stored here in a partially ordered structure.
     * See the definition of TimerHeap, above, for an explanation.
     */
    TimerHeap *heap;
    /*
     * The thread that watches the top of the heap, and processes
     * timers that have elapsed.
     */
    long thread;
};

struct Timer
{
	/*
	 * The timer set this timer belongs to.
	 */
	Timerset *timerset;
    /*
     * An event is produced on the output list when the
     * timer elapses.  The timer is not considered to have
     * elapsed completely until that pointer has also been
     * consumed from this list (by the caller, presumably).
     * That is why the timer code sometimes goes back and
     * removes a pointer from the output list.
     */
    List *output;
    /*
     * The timer is set to elapse at this time, expressed in
     * Unix time format.  This field is set to -1 if the timer
     * is not active (i.e. in the timer set's heap).
     */
    long elapses;
    /*
     * A duplicate of this event will be put on the output list
     * when the timer elapses.  It can be NULL if the timer has
     * not been started yet.
     */
    void *data;
    /*
     * This field is normally NULL, but after the timer elapses
     * it points to the event that was put on the output list.
     * It is set back to NULL if the event was taken back from
     * the list, or if it's confirmed that the event was consumed.
     */
    void *elapsed_data;
    /*
     * The index in the timer set's heap.  This field is managed by
     * the heap operations, and is used to make them faster.
     * If this timer is not in the heap, this field is -1.
     */
    long index;
};


/*
 * Internal functions
 */
static void abort_elapsed(Timer *timer);
static TimerHeap *heap_create(void);
static void heap_destroy(TimerHeap *heap);
static void heap_delete(TimerHeap *heap, long index);
static int heap_adjust(TimerHeap *heap, long index);
static void heap_insert(TimerHeap *heap, Timer *timer);
static void heap_swap(TimerHeap *heap, long index1, long index2);
static void lock(Timerset *set);
static void unlock(Timerset *set);
static void watch_timers(void *arg);   /* The timer thread */
static void elapse_timer(Timer *timer);


Timerset *gw_timerset_create(void)
{
	Timerset *set;

	set = gw_malloc(sizeof(Timerset));
    set->mutex = mutex_create();
    set->heap = heap_create();
    set->stopping = 0;
    set->thread = gwthread_create(watch_timers, set);

    return set;
}

void gw_timerset_destroy(Timerset *set)
{
	if (set == NULL)
		return;
       
    /* Stop all timers. */
    while (set->heap->len > 0)
        gw_timer_stop(set->heap->tab[0]);

    /* Kill timer thread */
    set->stopping = 1;
    gwthread_wakeup(set->thread);
    gwthread_join(set->thread);

    /* Free resources */
    heap_destroy(set->heap);
    mutex_destroy(set->mutex);
    gw_free(set);
}


Timer *gw_timer_create(Timerset *set, List *outputlist)
{
    Timer *t;

    t = gw_malloc(sizeof(*t));
    t->timerset = set;
    t->elapses = -1;
    t->data = NULL;
    t->elapsed_data = NULL;
    t->index = -1;
    t->output = outputlist;
    gwlist_add_producer(outputlist);

    return t;
}

void gw_timer_destroy(Timer *timer)
{
    if (timer == NULL)
        return;

    gw_timer_stop(timer);
    gwlist_remove_producer(timer->output);
    gw_free(timer);
}

void gw_timer_elapsed_destroy(Timer *timer)
{
    if (timer == NULL)
        return;

    gw_timer_elapsed_stop(timer);
    gwlist_remove_producer(timer->output);
    gw_free(timer);
}

void gw_timer_start(Timer *timer, int interval, void *data)
{
    int wakeup = 0;

    gw_assert(timer != NULL);

    if (timer == NULL)
        return;
        
    lock(timer->timerset);

    /* Convert to absolute time */
    interval += time(NULL);

    if (timer->elapses > 0) {
        /* Resetting an existing timer.  Move it to its new
         * position in the heap. */
        if (interval < timer->elapses && timer->index == 0)
            wakeup = 1;
        timer->elapses = interval;
        gw_assert(timer->index >= 0);
        gw_assert(timer->timerset->heap->tab[timer->index] == timer);
        wakeup |= heap_adjust(timer->timerset->heap, timer->index);
    } else {
        /* Setting a new timer, or resetting an elapsed one.
         * First deal with a possible elapse event that may
         * still be on the output list. */
        abort_elapsed(timer);

        /* Then activate the timer. */
        timer->elapses = interval;
        gw_assert(timer->index < 0);
        heap_insert(timer->timerset->heap, timer);
        wakeup = timer->index == 0;  /* Do we have a new top? */
    }

    if (data != NULL) {
        timer->data = data;
    }

    unlock(timer->timerset);

    if (wakeup)
        gwthread_wakeup(timer->timerset->thread);
}

void gw_timer_elapsed_start(Timer *timer, int interval, void *data)
{
    int wakeup = 0;

    gw_assert(timer != NULL);

    if (timer == NULL)
        return;

    lock(timer->timerset);

    /* Convert to absolute time */
    interval += time(NULL);

    if (timer->elapses > 0) {
        /* Resetting an existing timer.  Move it to its new
         * position in the heap. */
        if (interval < timer->elapses && timer->index == 0)
            wakeup = 1;
        timer->elapses = interval;
        gw_assert(timer->index >= 0);
        gw_assert(timer->timerset->heap->tab[timer->index] == timer);
        wakeup |= heap_adjust(timer->timerset->heap, timer->index);
    } else {
        /* Setting a new timer, or resetting an elapsed one.
         * There should be no further elapse event on the
         * output list here. */
        /* abort_elapsed(timer); */
    	timer->elapsed_data = NULL;

        /* Then activate the timer. */
        timer->elapses = interval;
        gw_assert(timer->index < 0);
        heap_insert(timer->timerset->heap, timer);
        wakeup = timer->index == 0;  /* Do we have a new top? */
    }

    if (data != NULL) {
        timer->data = data;
    }

    unlock(timer->timerset);

    if (wakeup)
        gwthread_wakeup(timer->timerset->thread);
}

void gw_timer_stop(Timer *timer)
{
    gw_assert(timer != NULL);
    lock(timer->timerset);

    /*
     * If the timer is active, make it inactive and remove it from
     * the heap.
     */
    if (timer->elapses > 0) {
        timer->elapses = -1;
        gw_assert(timer->timerset->heap->tab[timer->index] == timer);
        heap_delete(timer->timerset->heap, timer->index);
    }

    abort_elapsed(timer);

    unlock(timer->timerset);
}

void gw_timer_elapsed_stop(Timer *timer)
{
    gw_assert(timer != NULL);
    lock(timer->timerset);

    /*
     * If the timer is active, make it inactive and remove it from
     * the heap.
     */
    if (timer->elapses > 0) {
        timer->elapses = -1;
        gw_assert(timer->timerset->heap->tab[timer->index] == timer);
        heap_delete(timer->timerset->heap, timer->index);
    }

    /* abort_elapsed(timer); */
	timer->elapsed_data = NULL;

    unlock(timer->timerset);
}

List *gw_timer_break(Timerset *set)
{
	List *ret = NULL;

    lock(set);

    if (set->heap->len == 0) {
        unlock(set);
    	return NULL;
    }

    ret = gwlist_create();

    /* Stop all timers. */
    while (set->heap->len > 0) {
    	Timer *timer = set->heap->tab[0];

    	gwlist_append(ret, timer);

        /*
         * If the timer is active, make it inactive and remove it from
         * the heap.
         */
        if (timer->elapses > 0) {
            timer->elapses = -1;
            gw_assert(set->heap->tab[timer->index] == timer);
            heap_delete(set->heap, timer->index);
        }

        abort_elapsed(timer);
    }

    unlock(set);

    return ret;
}

void *gw_timer_data(Timer *timer)
{
    gw_assert(timer != NULL);

    return timer->data;
}

static void lock(Timerset *set)
{
    gw_assert(set != NULL);
    mutex_lock(set->mutex);
}

static void unlock(Timerset *set)
{
    gw_assert(set != NULL);
    mutex_unlock(set->mutex);
}

/*
 * Go back and remove this timer's elapse event from the output list,
 * to pretend that it didn't elapse after all.  This is necessary
 * to deal with some races between the timer thread and the caller's
 * start/stop actions.
 */
static void abort_elapsed(Timer *timer)
{
    if (timer->elapsed_data == NULL)
        return;

    gwlist_delete_equal(timer->output, timer->elapsed_data);
    timer->elapsed_data = NULL;
}

/*
 * Create a new timer heap.
 */
static TimerHeap *heap_create(void)
{
    TimerHeap *heap;

    heap = gw_malloc(sizeof(*heap));
    heap->tab = gw_malloc(sizeof(heap->tab[0]));
    heap->size = 1;
    heap->len = 0;

    return heap;
}

static void heap_destroy(TimerHeap *heap)
{
    if (heap == NULL)
        return;

    gw_free(heap->tab);
    gw_free(heap);
}

/*
 * Remove a timer from the heap.  Do this by swapping it with the element
 * in the last position, then shortening the heap, then moving the
 * swapped element up or down to maintain the partial ordering.
 */
static void heap_delete(TimerHeap *heap, long index)
{
    long last;

    gw_assert(index >= 0);
    gw_assert(index < heap->len);
    gw_assert(heap->tab[index]->index == index);

    last = heap->len - 1;
    heap_swap(heap, index, last);
    heap->tab[last]->index = -1;
    heap->len--;
    if (index != last)
        heap_adjust(heap, index);
}

/*
 * Add a timer to the heap.  Do this by adding it at the end, then
 * moving it up or down as necessary to achieve partial ordering.
 */
static void heap_insert(TimerHeap *heap, Timer *timer)
{
    heap->len++;
    if (heap->len > heap->size) {
        heap->tab = gw_realloc(heap->tab,
                                heap->len * sizeof(heap->tab[0]));
        heap->size = heap->len;
    }
    heap->tab[heap->len - 1] = timer;
    timer->index = heap->len - 1;
    heap_adjust(heap, timer->index);
}

/*
 * Swap two elements of the heap, and update their index fields.
 * This is the basic heap operation.
 */
static void heap_swap(TimerHeap *heap, long index1, long index2)
{
    Timer *t;

    gw_assert(index1 >= 0);
    gw_assert(index1 < heap->len);
    gw_assert(index2 >= 0);
    gw_assert(index2 < heap->len);

    if (index1 == index2)
        return;

    t = heap->tab[index1];
    heap->tab[index1] = heap->tab[index2];
    heap->tab[index2] = t;
    heap->tab[index1]->index = index1;
    heap->tab[index2]->index = index2;
}

/*
 * The current element has broken the partial ordering of the
 * heap (see explanation in the definition of Timerset), and
 * it has to be moved up or down until the ordering is restored.
 * Return 1 if the timer at the heap's top is now earlier than
 * before this operation, otherwise 0.
 */
static int heap_adjust(TimerHeap *heap, long index)
{
    Timer *t;
    Timer *parent;
    long child_index;

    /*
     * We can assume that the heap was fine before this element's
     * elapse time was changed.  There are three cases to deal
     * with:
     *  - Element's new elapse time is too small; it should be
     *    moved toward the top.
     *  - Element's new elapse time is too large; it should be
     *    moved toward the bottom.
     *  - Element's new elapse time still fits here, we don't
     *    have to do anything.
     */

    gw_assert(index >= 0);
    gw_assert(index < heap->len);

    /* Move to top? */
    t = heap->tab[index];
    parent = heap->tab[index / 2];
    if (t->elapses < parent->elapses) {
        /* This will automatically terminate when it reaches
         * the top, because in that t == parent. */
        do {
            heap_swap(heap, index, index / 2);
            index = index / 2;
            parent = heap->tab[index / 2];
        } while (t->elapses < parent->elapses);
        /* We're done.  Return 1 if we changed the top. */
        return index == 0;
    }

    /* Move to bottom? */
    for (; ; ) {
        child_index = index * 2;
        if (child_index >= heap->len)
            return 0;   /* Already at bottom */
        if (child_index == heap->len - 1) {
            /* Only one child */
            if (heap->tab[child_index]->elapses < t->elapses)
                heap_swap(heap, index, child_index);
            break;
        }

        /* Find out which child elapses first */
        if (heap->tab[child_index + 1]->elapses <
            heap->tab[child_index]->elapses) {
            child_index++;
        }

        if (heap->tab[child_index]->elapses < t->elapses) {
            heap_swap(heap, index, child_index);
            index = child_index;
        } else {
            break;
        }
    }

    return 0;
}

/*
 * This timer has elapsed.  Do the housekeeping.  We have its set locked.
 */
static void elapse_timer(Timer *timer)
{
    gw_assert(timer != NULL);
    gw_assert(timer->timerset != NULL);
    /* This must be true because abort_elapsed is always called
     * before a timer is activated. */
    gw_assert(timer->elapsed_data == NULL);

    timer->elapsed_data = timer->data;
    timer->elapses = -1;
    gwlist_produce(timer->output, timer->elapsed_data);
}

/*
 * Main function for timer thread.
 */
static void watch_timers(void *arg)
{
    Timerset *set;
    long top_time;
    long now;

    set = arg;

    while (!set->stopping) {
        lock(set);

        now = time(NULL);

        while (set->heap->len > 0 && set->heap->tab[0]->elapses <= now) {
        	Timer *timer = set->heap->tab[0];
        	heap_delete(set->heap, 0);
        	elapse_timer(timer);
        }

    	/*
    	 * Now sleep until the next timer elapses.  If there isn't one,
    	 * then just sleep very long.  We will get woken up if the
    	 * top of the heap changes before we wake.
    	 */

    	if (set->heap->len == 0) {
    		unlock(set);
    		gwthread_sleep(1000000.0);
    	} else {
    		top_time = set->heap->tab[0]->elapses;
    		unlock(set);
    		gwthread_sleep(top_time - now);
    	}
    }
}
