#pragma once


typedef int value_t;

class IQueue {
public:
    virtual ~IQueue() = default;

    virtual void queue_init() = 0;
    virtual void queue_destroy() = 0;
    virtual void thread_prepare() {}
    virtual void thread_cleanup() {}
    virtual void enq(value_t v) = 0;
    virtual int deq(value_t *v) = 0;
};
