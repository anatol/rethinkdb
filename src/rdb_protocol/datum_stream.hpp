#ifndef RDB_PROTOCOL_DATUM_STREAM_HPP_
#define RDB_PROTOCOL_DATUM_STREAM_HPP_

#include <algorithm>
#include <vector>

#include "rdb_protocol/stream.hpp"

namespace query_language {
class json_stream_t;
}

namespace ql {

class datum_stream_t : public ptr_baggable_t, public pb_rcheckable_t {
public:
    datum_stream_t(env_t *_env, const pb_rcheckable_t *backtrace_source)
        : pb_rcheckable_t(backtrace_source), env(_env) {
        guarantee(env);
    }
    virtual ~datum_stream_t() { }

    // stream -> stream
    virtual datum_stream_t *filter(func_t *f) = 0;
    virtual datum_stream_t *map(func_t *f) = 0;
    virtual datum_stream_t *concatmap(func_t *f) = 0;

    // stream -> atom
    virtual const datum_t *count() = 0;
    virtual const datum_t *reduce(val_t *base_val, func_t *f) = 0;
    virtual const datum_t *gmr(func_t *g, func_t *m, const datum_t *d, func_t *r) = 0;

    // stream -> stream (always eager)
    datum_stream_t *slice(size_t l, size_t r);
    datum_stream_t *zip();

    // Returns NULL if stream is lazy.
    virtual const datum_t *as_array() = 0;

    // Gets the next element from the stream.  (Wrapper around `next_impl`.)
    const datum_t *next();

    // Gets the next elements from the stream.  (Returns zero elements only when
    // the end of the stream has been reached.  Otherwise, returns at least one
    // element.)  (Wrapper around `next_batch_impl`.)
    std::vector<const datum_t *> next_batch();

protected:
    env_t *env;

private:
    static const size_t MAX_BATCH_SIZE = 100;

    // Returns NULL upon end of stream.
    virtual const datum_t *next_impl() = 0;
};

class eager_datum_stream_t : public datum_stream_t {
public:
    eager_datum_stream_t(env_t *env, const pb_rcheckable_t *backtrace_source)
        : datum_stream_t(env, backtrace_source) { }

    virtual datum_stream_t *filter(func_t *f);
    virtual datum_stream_t *map(func_t *f);
    virtual datum_stream_t *concatmap(func_t *f);

    virtual const datum_t *count();
    virtual const datum_t *reduce(val_t *base_val, func_t *f);
    virtual const datum_t *gmr(func_t *g, func_t *m, const datum_t *d, func_t *r);

    virtual const datum_t *as_array();
};

class map_datum_stream_t : public eager_datum_stream_t {
public:
    map_datum_stream_t(env_t *env, func_t *_f, datum_stream_t *_source)
        : eager_datum_stream_t(env, _source), f(_f), source(_source) {
        guarantee(f != NULL && source != NULL);
    }

private:
    const datum_t *next_impl();

    func_t *f;
    datum_stream_t *source;
};

class filter_datum_stream_t : public eager_datum_stream_t {
public:
    filter_datum_stream_t(env_t *env, func_t *_f, datum_stream_t *_source)
        : eager_datum_stream_t(env, _source), f(_f), source(_source) {
        guarantee(f && source);
    }

private:
    const datum_t *next_impl();

    func_t *f;
    datum_stream_t *source;
};

class concatmap_datum_stream_t : public eager_datum_stream_t {
public:
    concatmap_datum_stream_t(env_t *env, func_t *_f, datum_stream_t *_source)
        : eager_datum_stream_t(env, _source), f(_f), source(_source), subsource(NULL) {
        guarantee(f != NULL && source != NULL);
    }
private:
    const datum_t *next_impl();

    func_t *f;
    datum_stream_t *source;
    datum_stream_t *subsource;
};

class lazy_datum_stream_t : public datum_stream_t {
public:
    lazy_datum_stream_t(env_t *env, bool use_outdated,
                        namespace_repo_t<rdb_protocol_t>::access_t *ns_access,
                        const pb_rcheckable_t *bt_src);
    lazy_datum_stream_t(env_t *env, bool use_outdated,
                        namespace_repo_t<rdb_protocol_t>::access_t *ns_access,
                        const datum_t *pval, const std::string &sindex_id,
                        const pb_rcheckable_t *bt_src);
    virtual datum_stream_t *filter(func_t *f);
    virtual datum_stream_t *map(func_t *f);
    virtual datum_stream_t *concatmap(func_t *f);

    virtual const datum_t *count();
    virtual const datum_t *reduce(val_t *base_val, func_t *f);
    virtual const datum_t *gmr(func_t *g, func_t *m, const datum_t *base, func_t *r);
    virtual const datum_t *as_array() { return NULL; } // cannot be converted implicitly
private:
    const datum_t *next_impl();

    explicit lazy_datum_stream_t(const lazy_datum_stream_t *src);
    // To make the 1.4 release, this class was basically made into a shim
    // between the datum logic and the original json streams.
    boost::shared_ptr<query_language::json_stream_t> json_stream;

    // These are used on the json streams.  They're in the class instead of
    // being locally allocated because it makes debugging easier.

    rdb_protocol_t::rget_read_response_t::result_t run_terminal(const rdb_protocol_details::terminal_variant_t &t);
};

class array_datum_stream_t : public eager_datum_stream_t {
public:
    array_datum_stream_t(env_t *env, const datum_t *_arr, const pb_rcheckable_t *bt_src);

private:
    const datum_t *next_impl();

    size_t index;
    const datum_t *arr;
};

class slice_datum_stream_t : public eager_datum_stream_t {
public:
    slice_datum_stream_t(env_t *env, size_t left, size_t right, datum_stream_t *source);
private:
    const datum_t *next_impl();

    env_t *env;
    size_t index, left, right;
    datum_stream_t *source;
};

class zip_datum_stream_t : public eager_datum_stream_t {
public:
    zip_datum_stream_t(env_t *env, datum_stream_t *source);

private:
    const datum_t *next_impl();

    env_t *env;
    datum_stream_t *source;
};

// This has to be constructed explicitly rather than invoking `.sort()`.  There
// was a good reason for this involving header dependencies, but I don't
// remember exactly what it was.
static const size_t sort_el_limit = 1000000; // maximum number of elements we'll sort
template<class T>
class sort_datum_stream_t : public eager_datum_stream_t {
public:
    sort_datum_stream_t(env_t *env, const T &_lt_cmp, datum_stream_t *_src,
                        const pb_rcheckable_t *bt_src)
        : eager_datum_stream_t(env, bt_src), lt_cmp(_lt_cmp),
          src(_src), data_index(-1), is_arr_(false) {
        guarantee(src);
        load_data();
    }
    const datum_t *next_impl() {
        r_sanity_check(data_index >= 0);
        if (data_index >= static_cast<int>(data.size())) {
            //            ^^^^^^^^^^^^^^^^ this is safe because of `load_data`
            return NULL;
        } else {
            const datum_t *ret = data[data_index];
            ++data_index;
            return ret;
        }
    }
private:
    virtual const datum_t *as_array() {
        return is_arr() ? eager_datum_stream_t::as_array() : NULL;
    }
    bool is_arr() {
        return is_arr_;
    }
    void load_data() {
        if (data_index != -1) return;
        data_index = 0;
        if (const datum_t *arr = src->as_array()) {
            is_arr_ = true;
            rcheck(arr->size() <= sort_el_limit,
                   strprintf("Can only sort at most %zu elements.",
                             sort_el_limit));
            for (size_t i = 0; i < arr->size(); ++i) {
                data.push_back(arr->get(i));
            }
        } else {
            is_arr_ = false;
            size_t sort_els = 0;
            while (const datum_t *d = src->next()) {
                rcheck(++sort_els <= sort_el_limit,
                       strprintf("Can only sort at most %zu elements.",
                                 sort_el_limit));
                data.push_back(d);
            }
        }
        std::sort(data.begin(), data.end(), lt_cmp);
    }
    T lt_cmp;
    datum_stream_t *src;

    int data_index;
    std::vector<const datum_t *> data;
    bool is_arr_;
};

class union_datum_stream_t : public eager_datum_stream_t {
public:
    union_datum_stream_t(env_t *env, const std::vector<datum_stream_t *> &_streams,
                         const pb_rcheckable_t *bt_src)
        : eager_datum_stream_t(env, bt_src), streams(_streams), streams_index(0) { }
private:
    const datum_t *next_impl();

    std::vector<datum_stream_t *> streams;
    size_t streams_index;
};

} // namespace ql

#endif // RDB_PROTOCOL_DATUM_STREAM_HPP_
