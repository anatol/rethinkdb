#include "rdb_protocol/terms/seq.hpp"

#include <string>
#include <utility>
#include <vector>

#include "rdb_protocol/op.hpp"
#include "rdb_protocol/error.hpp"
#include "rdb_protocol/pb_utils.hpp"

namespace ql {

// Most of the real logic for these is in datum_stream.cc.

class count_term_t : public op_term_t {
public:
    count_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(1)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_seq()->count());
    }
    virtual const char *name() const { return "count"; }
};

class map_term_t : public op_term_t {
public:
    map_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(2)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_seq()->map(arg(1)->as_func()));
    }
    virtual const char *name() const { return "map"; }
};

class concatmap_term_t : public op_term_t {
public:
    concatmap_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(2)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_seq()->concatmap(arg(1)->as_func()));
    }
    virtual const char *name() const { return "concatmap"; }
};

class filter_term_t : public op_term_t {
public:
    filter_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(2)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        counted_t<val_t> v0 = arg(0);
        counted_t<val_t> v1 = arg(1);
        counted_t<func_t> f = v1->as_func(IDENTITY_SHORTCUT);
        if (v0->get_type().is_convertible(val_t::type_t::SELECTION)) {
            std::pair<counted_t<table_t>, counted_t<datum_stream_t> > ts
                = v0->as_selection();
            return new_val(ts.second->filter(f), ts.first);
        } else {
            return new_val(v0->as_seq()->filter(f));
        }
    }
    virtual const char *name() const { return "filter"; }
};

static const char *const reduce_optargs[] = {"base"};
class reduce_term_t : public op_term_t {
public:
    reduce_term_t(env_t *env, protob_t<const Term> term) :
        op_term_t(env, term, argspec_t(2), optargspec_t(reduce_optargs)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_seq()->reduce(optarg("base", counted_t<val_t>()),
                                                arg(1)->as_func()));
    }
    virtual const char *name() const { return "reduce"; }
};

// TODO: this sucks.  Change to use the same macros as rewrites.hpp?
static const char *const between_optargs[] = {"index"};
class between_term_t : public op_term_t {
public:
    between_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(3), optargspec_t(between_optargs)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        counted_t<table_t> tbl = arg(0)->as_table();
        counted_t<const datum_t> lb = arg(1)->as_datum();
        if (lb->get_type() == datum_t::R_NULL) {
            lb.reset();
        }
        counted_t<const datum_t> rb = arg(2)->as_datum();
        if (rb->get_type() == datum_t::R_NULL) {
            rb.reset();
        }
        if (!lb.has() && !rb.has()) {
            return new_val(tbl->as_datum_stream(), tbl);
        }

        counted_t<val_t> sindex = optarg("index", counted_t<val_t>());
        if (sindex.has()) {
            std::string sid = sindex->as_str();
            if (sid != tbl->get_pkey()) {
                return new_val(tbl->get_sindex_rows(lb, rb, sid, backtrace()), tbl);
            }
        }

        return new_val(tbl->get_rows(lb, rb, backtrace()), tbl);
    }
    virtual const char *name() const { return "between"; }

    protob_t<Term> filter_func;
};

class union_term_t : public op_term_t {
public:
    union_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(0, -1)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        std::vector<counted_t<datum_stream_t> > streams;
        for (size_t i = 0; i < num_args(); ++i) {
            streams.push_back(arg(i)->as_seq());
        }
        counted_t<datum_stream_t> union_stream
            = make_counted<union_datum_stream_t>(env, streams, backtrace());
        return new_val(union_stream);
    }
    virtual const char *name() const { return "union"; }
};

class zip_term_t : public op_term_t {
public:
    zip_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(1)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_seq()->zip());
    }
    virtual const char *name() const { return "zip"; }
};

counted_t<term_t> make_between_term(env_t *env, protob_t<const Term> term) {
    return make_counted<between_term_t>(env, term);
}
counted_t<term_t> make_reduce_term(env_t *env, protob_t<const Term> term) {
    return make_counted<reduce_term_t>(env, term);
}
counted_t<term_t> make_map_term(env_t *env, protob_t<const Term> term) {
    return make_counted<map_term_t>(env, term);
}
counted_t<term_t> make_filter_term(env_t *env, protob_t<const Term> term) {
    return make_counted<filter_term_t>(env, term);
}
counted_t<term_t> make_concatmap_term(env_t *env, protob_t<const Term> term) {
    return make_counted<concatmap_term_t>(env, term);
}
counted_t<term_t> make_count_term(env_t *env, protob_t<const Term> term) {
    return make_counted<count_term_t>(env, term);
}
counted_t<term_t> make_union_term(env_t *env, protob_t<const Term> term) {
    return make_counted<union_term_t>(env, term);
}
counted_t<term_t> make_zip_term(env_t *env, protob_t<const Term> term) {
    return make_counted<zip_term_t>(env, term);
}

} //namespace ql
