#include "containers/ptr_bag.hpp"

// #define PTR_BAG_LOG 1

#ifdef PTR_BAG_LOG
#include "activity_logger.hpp"
static activity_logger_t ptr_bag_log;
#define pblog(args...) do { \
        activity_logger_t *_logptr = &(ptr_bag_log);                            \
        std::string _debugf_log = strprintf(args);                              \
        _logptr->add(_debugf_log);                                              \
        debugf("%p[%zu]: %s\n", _logptr, _logptr->size(), _debugf_log.c_str()); \
    } while (0)
#else
#define pblog(...)
#endif // PTR_BAG_LOG

ptr_bag_t::ptr_bag_t() : parent(0), mem_estimate(0) {
    pblog("%p created", this);
}
ptr_bag_t::~ptr_bag_t() {
    assert_thread();
    guarantee(!parent || ptrs.size() == 0);
    for (std::set<ptr_baggable_t *>::iterator
             it = ptrs.begin(); it != ptrs.end(); ++it) {
        pblog("deleting %p from %p", *it, this);
        delete *it;
    }
}

bool ptr_bag_t::has(const ptr_baggable_t *ptr) {
    if (parent) return parent->has(ptr);
    return ptrs.count(const_cast<ptr_baggable_t *>(ptr)) > 0;
}
void ptr_bag_t::yield_to(ptr_bag_t *new_bag, const ptr_baggable_t *ptr) {
    size_t num_erased = ptrs.erase(const_cast<ptr_baggable_t *>(ptr));
    guarantee(num_erased == 1);
    new_bag->add(ptr);
}

std::string ptr_bag_t::print_debug() const {
    std::string acc = strprintf("%zu(%zu) [", ptrs.size(), mem_estimate);
    for (std::set<ptr_baggable_t *>::const_iterator
             it = ptrs.begin(); it != ptrs.end(); ++it) {
        acc += (it == ptrs.begin() ? "" : ", ") + strprintf("%p", *it);
    }
    return acc + "]";
}

size_t ptr_bag_t::get_mem_estimate() const {
    return parent
        ? parent->get_mem_estimate()
        : (mem_estimate * mem_estimate_multiplier);
}

void ptr_bag_t::real_add(ptr_baggable_t *ptr, size_t _mem_estimate) {
    pblog("adding %p to %p", ptr, this);
    assert_thread();
    if (parent) {
        guarantee(ptrs.size() == 0);
        guarantee(mem_estimate == 0);
        parent->real_add(ptr, _mem_estimate);
    } else {
        guarantee(ptrs.count(ptr) == 0);
        ptrs.insert(static_cast<ptr_baggable_t *>(ptr));
        mem_estimate += _mem_estimate;
    }
}

void ptr_bag_t::shadow(ptr_bag_t *_parent, size_t *bag_size_out) {
    parent = _parent;
    for (std::set<ptr_baggable_t *>::iterator
             it = ptrs.begin(); it != ptrs.end(); ++it) {
        parent->real_add(*it, 0);
    }
    ptrs = std::set<ptr_baggable_t *>();
    *bag_size_out = mem_estimate;
    mem_estimate = 0;
}

void debug_print(append_only_printf_buffer_t *buf, const ptr_bag_t &pbag) {
    buf->appendf("%s", pbag.print_debug().c_str());
}
