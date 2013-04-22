#ifndef RDB_PROTOCOL_TERMS_JS_HPP_
#define RDB_PROTOCOL_TERMS_JS_HPP_

#include <string>

#include "errors.hpp"
#include <boost/variant/static_visitor.hpp>

#include "rdb_protocol/op.hpp"
#include "rdb_protocol/error.hpp"
#include "rdb_protocol/js.hpp"

namespace ql {

class javascript_term_t : public op_term_t {
public:
    javascript_term_t(env_t *env, const Term *term)
        : op_term_t(env, term, argspec_t(1)) { }
private:

    virtual val_t *eval_impl() {
        std::string source = arg(0)->as_datum()->as_str();

        boost::shared_ptr<js::runner_t> js = env->get_js_runner();
        js::js_result_t result = js->eval(source);

        return boost::apply_visitor(js_result_visitor_t(env, this), result);
    }
    virtual const char *name() const { return "javascript"; }

    // No JS term is considered deterministic
    virtual bool is_deterministic_impl() const {
        return false;
    }
};

}

#endif // RDB_PROTOCOL_TERMS_JS_HPP_
