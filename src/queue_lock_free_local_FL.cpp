// Exercise 5 – Concurrent lock free queue with local free lists
// To solve the ABA problems tagged pointers have been used

#include "queue_lock_free_local_FL.h"
#include <stdio.h>
#include <assert.h>

thread_local QueueLockFreeLocalFL::TLFL free_list;

// Initialize queue
void QueueLockFreeLocalFL::queue_init() {
    IQueue::queue_init();
    node_t* sent = allocate_node();
    TP tailTptr = TP::pack_pointer(sent, 0);
    atomic_init(&this->tail_tp, tailTptr);
    
    TP headTptr = TP::pack_pointer(sent, 0);
    atomic_init(&this->head_tp, headTptr);
}

// Destroy queue and free queue
void QueueLockFreeLocalFL::queue_destroy() {
    // Free main list
    node_t *n = TP::extract_address(this->head_tp.load());

    while (n) {
        node_t *tmp = node_t::TPN::extract_address(n->next_tp.load());
        free(n);
        n = tmp;
    }
}

void QueueLockFreeLocalFL::thread_prepare() {
    IQueue::thread_prepare();
    free_list.reset(); // free_list is reset since omp likes to reuse threads
}

void QueueLockFreeLocalFL::thread_cleanup() {
    IQueue::thread_cleanup();
}

void QueueLockFreeLocalFL::enq(value_t v) {
    // prepare node
    node_t *n = get_node();
    n->v = v;

    // insert it into the queue
    while(true) {
        TP tailTptr = this->tail_tp.load();
        node_t* tail = TP::extract_address(tailTptr);
        node_t::TPN nextTptr = tail->next_tp.load();
        node_t* next = node_t::TPN::extract_address(nextTptr);
        assert(tail != next); // check if a node is pointing to itself

        if (next == NULL) {
            // try to insert node
            assert(n != next);

            size_t tag = node_t::TPN::extract_tag(nextTptr);
            node_t::TPN newNextTptr = node_t::TPN::pack_pointer(n, tag + 1);
            if (tail->next_tp.compare_exchange_strong(nextTptr, newNextTptr)) {
                tls_stats.successful_CAS_ops++;
                // node sucessfully added
                size_t oldTag = TP::extract_tag(tailTptr);
                TP newTailTPtr = TP::pack_pointer(n, oldTag + 1);
                this->tail_tp.compare_exchange_strong(tailTptr, newTailTPtr);
                
                return;
            } else {
                tls_stats.failed_CAS_ops++;
                // another thread was faster
            }
        } 
        else {
            // help move tail to end
            helpMoveTail(tailTptr, next);
        }
    }
}

// Dequeue protected by dequeue lock 
int QueueLockFreeLocalFL::deq(value_t *v) {

    while (true) {
        TP headTptr = this->head_tp.load();
        TP tailTptr = this->tail_tp.load();
        node_t* head = TP::extract_address(headTptr);
        node_t::TPN nextTptr = head->next_tp.load(); // always defined due to sentinel
        node_t* next = node_t::TPN::extract_address(nextTptr);
        node_t* tail = TP::extract_address(tailTptr);

        if (head == tail) {
            if (next == NULL) {
                // queue is empty
                
                return 0;
            }
            // tail is lagging behind, move to not dequeue it
            helpMoveTail(tailTptr, next);
        }
        else {
            // queue has values
            if (next == NULL) {
                // Either:
                // head was dequeued by another thread inbetween loads
                // -> queue has unknown status, retry?
                // Or:
                // queue split ABA problem happened
                // in that case head and tail are not pointing to the same queue 
                // and no further dequeues can be made
                continue; // in case of an ABA problem this leads to an infinite loop
                //return 0;
            }
            value_t val = next->v; // since nodes are only freed at the end, this always points to a valid object

            // move head and increase its tag
            size_t oldTag = TP::extract_tag(headTptr);
            TP newHeadTPtr = TP::pack_pointer(next, oldTag + 1);
            assert(TP::extract_address(newHeadTPtr) == next);
            assert(headTptr != newHeadTPtr);
            if (this->head_tp.compare_exchange_strong(headTptr, newHeadTPtr)) {
                tls_stats.successful_CAS_ops++;
                // successfully dequeued node
                
                free_node(head);
                *v = val;
                
                return 1;
            }
            else {
                tls_stats.failed_CAS_ops++;
                // failed to dequeue, retry
            }
        }
    }
}

void QueueLockFreeLocalFL::helpMoveTail(QueueLockFreeLocalFL::TP &tailTptr, QueueLockFreeLocalFL::node_t *next) {
    size_t oldTag = TP::extract_tag(tailTptr);
    TP newTailTPtr = TP::pack_pointer(next, oldTag + 1);
    if (this->tail_tp.compare_exchange_strong(tailTptr, newTailTPtr))
        tls_stats.successful_CAS_ops++;
    else
        tls_stats.failed_CAS_ops++;
}

// returns either a node from the free list or creates a new one
// returned nodes next addresses will be set to NULL
QueueLockFreeLocalFL::node_t* QueueLockFreeLocalFL::get_node() {
    node_t *n = free_list.pop();
    if (!n) {
        n = allocate_node();
        tls_stats.malloc_count++;
    } else {
        tls_stats.reused_count++;
    }
    
    size_t oldTag = node_t::TPN::extract_tag(n->next_tp.load());
    node_t::TPN newTptr = node_t::TPN::pack_pointer(NULL, oldTag + 1);
    n->next_tp.store(newTptr);
    return n;
}

void QueueLockFreeLocalFL::free_node(node_t *n) {
    free_list.push(n);
}

QueueLockFreeLocalFL::node_t *QueueLockFreeLocalFL::allocate_node() {
    node_t* n = (node_t*)aligned_alloc(OBJ_ALIGNMENT, sizeof(node_t));
    if (!n) { perror("aligned_alloc"); abort(); }
    // check alignment
    assert((intptr_t)n % OBJ_ALIGNMENT == 0);
    assert(((intptr_t)n & LOWER_TAG_MASK) == (intptr_t)NULL); 
    atomic_init(&n->next_tp, node_t::TPN::pack_pointer(NULL, 0));
    
    return n;
}
