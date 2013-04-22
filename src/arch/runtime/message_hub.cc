// Copyright 2010-2012 RethinkDB, all rights reserved.
#include "arch/runtime/message_hub.hpp"

#include <math.h>
#include <unistd.h>

#include "config/args.hpp"
#include "arch/runtime/event_queue.hpp"
#include "arch/runtime/thread_pool.hpp"
#include "logger.hpp"

// Set this to 1 if you would like some "unordered" messages to be unordered.
#ifndef NDEBUG
#define RDB_RELOOP_MESSAGES 0
#endif

linux_message_hub_t::linux_message_hub_t(linux_event_queue_t *queue, linux_thread_pool_t *thread_pool, int current_thread)
    : queue_(queue), thread_pool_(thread_pool), current_thread_(current_thread) {

    // We have to do this through dynamically, otherwise we might
    // allocate far too many file descriptors since this is what the
    // constructor of the system_event_t object (which is a member of
    // notify_t) does.
    notify_ = new notify_t[thread_pool_->n_threads];

    for (int i = 0; i < thread_pool_->n_threads; i++) {
        // Create notify fd for other cores that send work to us
        notify_[i].notifier_thread = i;
        notify_[i].parent = this;

        queue_->watch_resource(notify_[i].event.get_notify_fd(), poll_event_in, &notify_[i]);
    }
}

linux_message_hub_t::~linux_message_hub_t() {
    for (int i = 0; i < thread_pool_->n_threads; i++) {
        guarantee(queues_[i].msg_local_list.empty());
    }

    guarantee(incoming_messages_.empty());

    delete[] notify_;
}

void linux_message_hub_t::do_store_message(unsigned int nthread, linux_thread_message_t *msg) {
    rassert(nthread < (unsigned)thread_pool_->n_threads);
    queues_[nthread].msg_local_list.push_back(msg);
}

// Collects a message for a given thread onto a local list.
void linux_message_hub_t::store_message(unsigned int nthread, linux_thread_message_t *msg) {
#ifndef NDEBUG
#if RDB_RELOOP_MESSAGES
    // We default to 1, not zero, to allow store_message_sometime messages to sometimes jump ahead of
    // store_message messages.
    msg->reloop_count_ = 1;
#else
    msg->reloop_count_ = 0;
#endif
#endif  // NDEBUG
    do_store_message(nthread, msg);
}

int rand_reloop_count() {
    int x;
    frexp(randint(10000) / 10000.0, &x);
    int ret = -x;
    rassert(ret >= 0);
    return ret;
}

void linux_message_hub_t::store_message_sometime(unsigned int nthread, linux_thread_message_t *msg) {
#ifndef NDEBUG
#if RDB_RELOOP_MESSAGES
    msg->reloop_count_ = rand_reloop_count();
#else
    msg->reloop_count_ = 0;
#endif
#endif  // NDEBUG
    do_store_message(nthread, msg);
}


void linux_message_hub_t::insert_external_message(linux_thread_message_t *msg) {
    {
        spinlock_acq_t acq(&incoming_messages_lock_);
        incoming_messages_.push_back(msg);
    }

    // Wakey wakey eggs and bakey
    notify_[current_thread_].event.wakey_wakey();
}

void linux_message_hub_t::notify_t::on_event(int events) {

    if (events != poll_event_in) {
        logERR("Unexpected event mask: %d", events);
    }

    // Read from the event so level-triggered mechanism such as poll
    // don't pester us and use 100% cpu
    event.consume_wakey_wakeys();

    msg_list_t msg_list;

    // Pull the messages
    {
        spinlock_acq_t acq(&parent->incoming_messages_lock_);
        msg_list.append_and_clear(&parent->incoming_messages_);
    }

#ifndef NDEBUG
    start_watchdog(); // Initialize watchdog before handling messages
#endif

    while (linux_thread_message_t *m = msg_list.head()) {
        msg_list.remove(m);
#ifndef NDEBUG
        if (m->reloop_count_ > 0) {
            --m->reloop_count_;
            parent->do_store_message(parent->current_thread_, m);
            continue;
        }
#endif

        m->on_thread_switch();

#ifndef NDEBUG
        pet_watchdog(); // Verify that each message completes in the acceptable time range
#endif
    }
}

// Pushes messages collected locally global lists available to all
// threads.
void linux_message_hub_t::push_messages() {
    for (int i = 0; i < thread_pool_->n_threads; i++) {
        // Append the local list for ith thread to that thread's global
        // message list.
        thread_queue_t *queue = &queues_[i];
        if (!queue->msg_local_list.empty()) {
            // Transfer messages to the other core

            bool do_wake_up;
            {
                spinlock_acq_t acq(&thread_pool_->threads[i]->message_hub.incoming_messages_lock_);

                //We only need to do a wake up if the global
                do_wake_up = thread_pool_->threads[i]->message_hub.incoming_messages_.empty();

                thread_pool_->threads[i]->message_hub.incoming_messages_.append_and_clear(&queue->msg_local_list);
            }

            // Wakey wakey, perhaps eggs and bakey
            if (do_wake_up) {
                thread_pool_->threads[i]->message_hub.notify_[current_thread_].event.wakey_wakey();
            }
        }
    }
}
