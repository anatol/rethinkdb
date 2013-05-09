#include "rdb_protocol/terms/obj.hpp"

#include "rdb_protocol/op.hpp"
#include "rdb_protocol/error.hpp"

namespace ql {

class getattr_term_t : public op_term_t {
public:
    getattr_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(2)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        return new_val(arg(0)->as_datum()->get(arg(1)->as_str()));
    }
    virtual const char *name() const { return "getattr"; }
};

class contains_term_t : public op_term_t {
public:
    contains_term_t(env_t *env, protob_t<const Term> term)
        : op_term_t(env, term, argspec_t(1, -1)) { }
private:
    virtual counted_t<val_t> eval_impl() {
        counted_t<const datum_t> obj = arg(0)->as_datum();
        bool contains = true;
        for (size_t i = 1; i < num_args(); ++i) {
            contains = contains && obj->get(arg(i)->as_str(), NOTHROW).has();
        }
        return new_val(make_counted<const datum_t>(datum_t::R_BOOL, contains));
    }
    virtual const char *name() const { return "contains"; }
};

counted_t<term_t> make_getattr_term(env_t *env, protob_t<const Term> term) {
    return make_counted<getattr_term_t>(env, term);
}

counted_t<term_t> make_contains_term(env_t *env, protob_t<const Term> term) {
    return make_counted<contains_term_t>(env, term);
}

} // namespace ql
