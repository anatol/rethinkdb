#ifndef CONTAINERS_PTR_BAG_HPP_
#define CONTAINERS_PTR_BAG_HPP_

#include <set>
#include <string>

#include "utils.hpp"

// Classes that can be put into a pointer bag should inherit from this.
class ptr_baggable_t {
public:
    virtual ~ptr_baggable_t() { }
};

// A pointer bag holds a bunch of pointers and deletes them when it's freed.
class ptr_bag_t : public ptr_baggable_t, private home_thread_mixin_t {
public:
    ptr_bag_t();
    ~ptr_bag_t();

    // We want to be able to add const pointers to the bag too.
    template<class T>
    const T *add(const T *ptr) { return add(const_cast<T *>(ptr)); }
    // Add a pointer to the bag; it will be deleted when the bag is destroyed.
    template<class T>
    T *add(T *ptr) {
        real_add(static_cast<ptr_baggable_t *>(ptr), sizeof(T));
        return ptr;
    }

    // We want to make sure that if people add `p` to ptr_bag `A`, then add `A`
    // to ptr_bag `B`, then add `p` to `B`, that `p` doesn't get double-freed.
    ptr_bag_t *add(ptr_bag_t *sub_bag) {
        size_t sub_bag_size;
        sub_bag->shadow(this, &sub_bag_size);
        real_add(sub_bag, sizeof(ptr_bag_t) + sub_bag_size);
        return sub_bag;
    }

    bool has(const ptr_baggable_t *ptr);
    void yield_to(ptr_bag_t *new_bag, const ptr_baggable_t *ptr);
    std::string print_debug() const;

    // This is a bullshit constant.  We assume the memory usage of `T` is
    // `sizeof(T) * mem_estimate_multiplier`.  This will be improved for explain.
    static const int mem_estimate_multiplier = 2;
    size_t get_mem_estimate() const;
private:
    void real_add(ptr_baggable_t *ptr, size_t _mem_estimate);

    // When a ptr_bag shadows another ptr_bag, any pointers inserted into it in
    // the past or future are inserted into the parent ptr_bag.
    void shadow(ptr_bag_t *_parent, size_t *bag_size_out);
    ptr_bag_t *parent; // See `shadow`.
    std::set<ptr_baggable_t *> ptrs;
    size_t mem_estimate;

    DISABLE_COPYING(ptr_bag_t);
};

void debug_print(append_only_printf_buffer_t *buf, const ptr_bag_t &pbag);

#endif // CONTAINERS_PTR_BAG_HPP_
