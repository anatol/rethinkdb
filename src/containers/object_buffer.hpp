// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef CONTAINERS_OBJECT_BUFFER_HPP_
#define CONTAINERS_OBJECT_BUFFER_HPP_

#include "errors.hpp"

// Caveat: do not use this template with an object that has a blocking destructor, if
//  you are going to allocate multiple times using a single object_buffer_t.  This object
//  should catch it if you try to do anything particularly stupid, though.
template <class T>
class object_buffer_t {
public:
    class destruction_sentinel_t {
    public:
        explicit destruction_sentinel_t(object_buffer_t<T> *_parent) : parent(_parent) { }

        ~destruction_sentinel_t() {
            if (parent->has()) {
                parent->reset();
            }
        }
    private:
        object_buffer_t<T> *parent;

        DISABLE_COPYING(destruction_sentinel_t);
    };

    object_buffer_t() : state(EMPTY) { }
    ~object_buffer_t() {
        // The buffer cannot be destroyed while an object is in the middle of
        //  constructing or destructing
        if (state == INSTANTIATED) {
            reset();
        } else {
            rassert(state == EMPTY);
        }
    }

    template <class... Args>
    T *create(const Args &... args) {
        rassert(state == EMPTY);
        state = CONSTRUCTING;
        new (&object_data[0]) T(args...);
        state = INSTANTIATED;
        return get();
    }

    T *get() {
        rassert(state == INSTANTIATED);
        return reinterpret_cast<T *>(&object_data[0]);
    }

    T *operator->() {
        return get();
    }

    const T *get() const {
        rassert(state == INSTANTIATED);
        return reinterpret_cast<const T *>(&object_data[0]);
    }

    void reset() {
        T *obj_ptr = get();
        state = DESTRUCTING;
        obj_ptr->~T();
        state = EMPTY;
    }

    bool has() const {
        return (state == INSTANTIATED);
    }

private:
    // We're going more for a high probability of good alignment than
    // proof of good alignment.
    char object_data[sizeof(T)];

    enum buffer_state_t {
        EMPTY,
        CONSTRUCTING,
        INSTANTIATED,
        DESTRUCTING
    } state;

    DISABLE_COPYING(object_buffer_t);
};

#endif  // CONTAINERS_OBJECT_BUFFER_HPP_
