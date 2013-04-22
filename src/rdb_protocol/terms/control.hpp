#ifndef RDB_PROTOCOL_TERMS_CONTROL_HPP_
#define RDB_PROTOCOL_TERMS_CONTROL_HPP_

#include <vector>

#include "rdb_protocol/op.hpp"
#include "rdb_protocol/error.hpp"

namespace ql {

// ALL and ANY are written strangely because I originally thought that we could
// have non-boolean values that evaluate to true, but then we decided not to do
// that.

class all_term_t : public op_term_t {
public:
    all_term_t(env_t *env, const Term *term)
        : op_term_t(env, term, argspec_t(1, -1)) { }
private:
    virtual val_t *eval_impl() {
        for (size_t i = 0; i < num_args(); ++i) {
            env_checkpoint_t ect(env, env_checkpoint_t::DISCARD);
            val_t *v = arg(i);
            if (!v->as_bool()) {
                ect.reset(env_checkpoint_t::MERGE);
                return v;
            } else if (i == num_args() - 1) {
                ect.reset(env_checkpoint_t::MERGE);
                return v;
            }
        }
        unreachable();
    }
    virtual const char *name() const { return "all"; }
};

class any_term_t : public op_term_t {
public:
    any_term_t(env_t *env, const Term *term)
        : op_term_t(env, term, argspec_t(1, -1)) { }
private:
    virtual val_t *eval_impl() {
        for (size_t i = 0; i < num_args(); ++i) {
            env_checkpoint_t ect(env, env_checkpoint_t::DISCARD);
            val_t *v = arg(i);
            if (v->as_bool()) {
                ect.reset(env_checkpoint_t::MERGE);
                return v;
            }
        }
        return new_val_bool(false);
    }
    virtual const char *name() const { return "any"; }
};

class branch_term_t : public op_term_t {
public:
    branch_term_t(env_t *env, const Term *term) : op_term_t(env, term, argspec_t(3)) { }
private:
    virtual val_t *eval_impl() {
        bool b;
        {
            env_checkpoint_t ect(env, env_checkpoint_t::DISCARD);
            b = arg(0)->as_bool();
        }
        return b ? arg(1) : arg(2);
    }
    virtual const char *name() const { return "branch"; }
};


class funcall_term_t : public op_term_t {
public:
    funcall_term_t(env_t *env, const Term *term)
        : op_term_t(env, term, argspec_t(1, -1)) { }
private:
    virtual val_t *eval_impl() {
        func_t *f = arg(0)->as_func(IDENTITY_SHORTCUT);
        std::vector<const datum_t *> args;
        for (size_t i = 1; i < num_args(); ++i) args.push_back(arg(i)->as_datum());
        return f->call(args);
    }
    virtual const char *name() const { return "funcall"; }
};

} // namespace ql

#endif // RDB_PROTOCOL_TERMS_CONTROL_HPP_
