#include "rdb_protocol/env.hpp"
#include "rdb_protocol/pb_utils.hpp"
#include "rdb_protocol/term_walker.hpp"

#pragma GCC diagnostic ignored "-Wshadow"

namespace ql {

bool env_t::add_optarg(const std::string &key, const Term &val) {
    if (optargs.count(key)) return true;
    env_wrapper_t<Term> *ewt = add_ptr(new env_wrapper_t<Term>());
    Term *arg = &ewt->t;
    N2(FUNC, N0(MAKE_ARRAY), *arg = val);
    term_walker_t(arg, &val.GetExtension(ql2::extension::backtrace));
    optargs[key] = wire_func_t(*arg, 0);
    return false;
}
void env_t::init_optargs(const std::map<std::string, wire_func_t> &_optargs) {
    r_sanity_check(optargs.size() == 0);
    optargs = _optargs;
}
val_t *env_t::get_optarg(const std::string &key){
    if (!optargs.count(key)) return 0;
    return optargs[key].compile(this)->call();
}
const std::map<std::string, wire_func_t> &env_t::get_all_optargs() {
    return optargs;
}


static const int min_normal_gensym = -1000000;
int env_t::gensym(bool allow_implicit) {
    r_sanity_check(0 > next_gensym_val && next_gensym_val >= min_normal_gensym);
    int gensym = next_gensym_val--;
    if (!allow_implicit) {
        gensym += min_normal_gensym;
        r_sanity_check(gensym < min_normal_gensym);
    }
    return gensym;
}

bool env_t::var_allows_implicit(int varnum) {
    return varnum >= min_normal_gensym;
}

void env_t::push_implicit(const datum_t **val) {
    implicit_var.push(val);
}
const datum_t **env_t::top_implicit(const rcheckable_t *caller) {
    rcheck_target(caller, !implicit_var.empty(),
                  "r.row is not defined in this context.");
    rcheck_target(caller, implicit_var.size() == 1,
                  "Cannot use r.row in nested queries.  Use functions instead.");
    return implicit_var.top();
}
void env_t::pop_implicit() {
    r_sanity_check(implicit_var.size() > 0);
    implicit_var.pop();
}

size_t env_t::num_checkpoints() const {
    return bags.size()-1;
}
bool env_t::some_bag_has(const ptr_baggable_t *p) {
    for (size_t i = 0; i < bags.size(); ++i) if (bags[i]->has(p)) return true;
    return false;
}
void env_t::checkpoint() {
    bags.push_back(new ptr_bag_t());
}

void env_t::merge_checkpoint() {
    r_sanity_check(bags.size() >= 2);
    bags[bags.size()-2]->add(bags[bags.size()-1]);
    bags.pop_back();
}
void env_t::discard_checkpoint() {
    r_sanity_check(bags.size() >= 2);
    delete bags[bags.size()-1];
    bags.pop_back();
}

bool env_t::gc_callback(const datum_t *el) {
    if (old_bag->has(el)) {
        old_bag->yield_to(new_bag, el);
        return true;
    }
    r_sanity_check(some_bag_has(el));
    return false;
}
void env_t::gc(const datum_t *root) {
    old_bag = current_bag();
    scoped_ptr_t<ptr_bag_t> _new_bag(new ptr_bag_t);
    new_bag = _new_bag.get();
    root->iter(gc_callback_caller_t(this));
    *current_bag_ptr() = _new_bag.release();
    delete old_bag;
}

ptr_bag_t *env_t::current_bag() { return *current_bag_ptr(); }
ptr_bag_t **env_t::current_bag_ptr() {
    r_sanity_check(bags.size() > 0);
    return &bags[bags.size()-1];
}

void env_t::push_var(int var, const datum_t **val) {
    vars[var].push(val);
}
const datum_t **env_t::top_var(int var, const rcheckable_t *caller) {
    rcheck_target(caller, !vars[var].empty(),
                  strprintf("Unrecognized variabled %d", var));
    return vars[var].top();
}
void env_t::pop_var(int var) {
    vars[var].pop();
}
void env_t::dump_scope(std::map<int64_t, const datum_t **> *out) {
    for (std::map<int64_t, std::stack<const datum_t **> >::iterator
             it = vars.begin(); it != vars.end(); ++it) {
        if (it->second.size() == 0) continue;
        r_sanity_check(it->second.top());
        (*out)[it->first] = it->second.top();
    }
}
void env_t::push_scope(std::map<int64_t, Datum> *in) {
    scope_stack.push(std::vector<std::pair<int, const datum_t *> >());

    for (std::map<int64_t, Datum>::iterator it = in->begin(); it != in->end(); ++it) {
        const datum_t *d = add_ptr(new datum_t(&it->second, this));
        scope_stack.top().push_back(std::make_pair(it->first, d));
    }

    for (size_t i = 0; i < scope_stack.top().size(); ++i) {
        //        &scope_stack.top()[i].second,
        //        scope_stack.top()[i].second);
        push_var(scope_stack.top()[i].first, &scope_stack.top()[i].second);
    }
}
void env_t::pop_scope() {
    r_sanity_check(scope_stack.size() > 0);
    for (size_t i = 0; i < scope_stack.top().size(); ++i) {
        pop_var(scope_stack.top()[i].first);
    }
    // DO NOT pop the vector off the scope stack.  You might invalidate a
    // pointer too early.
}

void env_t::set_eval_callback(eval_callback_t *callback) {
    eval_callback = callback;
}

void env_t::do_eval_callback() {
    if (eval_callback != NULL) {
        eval_callback->eval_callback();
    }
}

env_checkpoint_t::env_checkpoint_t(env_t *_env, destructor_op_t _destructor_op)
    : env(_env), destructor_op(_destructor_op) {
    env->checkpoint();
}
env_checkpoint_t::~env_checkpoint_t() {
    switch (destructor_op) {
    case MERGE: {
        env->merge_checkpoint();
    } break;
    case DISCARD: {
        env->discard_checkpoint();
    } break;
    default: unreachable();
    }
}
void env_checkpoint_t::reset(destructor_op_t new_destructor_op) {
    destructor_op = new_destructor_op;
}
void env_checkpoint_t::gc(const datum_t *root) {
    env->gc(root);
}

// We GC more frequently (~ every 16 data) in debug mode to help with testing.
#ifndef NDEBUG
const int env_gc_checkpoint_t::DEFAULT_GEN1_CUTOFF =
    sizeof(datum_t) * ptr_bag_t::mem_estimate_multiplier * 16;
#else
const int env_gc_checkpoint_t::DEFAULT_GEN1_CUTOFF = (8 * 1024 * 1024);
#endif // NDEBUG
const int env_gc_checkpoint_t::DEFAULT_GEN2_SIZE_MULTIPLIER = 8;

env_gc_checkpoint_t::env_gc_checkpoint_t(env_t *_env, size_t _gen1, size_t _gen2)
    : finalized(false), env(_env), gen1(_gen1), gen2(_gen2) {
    r_sanity_check(env);
    if (!gen1) gen1 = DEFAULT_GEN1_CUTOFF;
    if (!gen2) gen2 = DEFAULT_GEN2_SIZE_MULTIPLIER * gen1;
    env->checkpoint(); // gen2
    env->checkpoint(); // gen1
}
env_gc_checkpoint_t::~env_gc_checkpoint_t() {
    // We might not be finalized if e.g. an exception was thrown.
    if (!finalized) {
        env->merge_checkpoint();
        env->merge_checkpoint();
    }
}

const datum_t *env_gc_checkpoint_t::maybe_gc(const datum_t *root) {
    if (env->current_bag()->get_mem_estimate() > gen1) {
        env->gc(root);
        env->merge_checkpoint();
        if (env->current_bag()->get_mem_estimate() > gen2) {
            env->gc(root);
            if (env->current_bag()->get_mem_estimate() > (gen2 * 2 / 3)) {
                gen2 *= 4;
            }
        }
        env->checkpoint();
    }
    return root;
}
const datum_t *env_gc_checkpoint_t::finalize(const datum_t *root) {
    r_sanity_check(!finalized);
    finalized = true;
    if (root) env->gc(root);
    env->merge_checkpoint();
    if (root) env->gc(root);
    env->merge_checkpoint();
    return root;
}

void env_t::join_and_wait_to_propagate(
    const cluster_semilattice_metadata_t &metadata_to_join)
    THROWS_ONLY(interrupted_exc_t) {
    cluster_semilattice_metadata_t sl_metadata;
    {
        on_thread_t switcher(semilattice_metadata->home_thread());
        semilattice_metadata->join(metadata_to_join);
        sl_metadata = semilattice_metadata->get();
    }

    boost::function<bool (const cow_ptr_t<ns_metadata_t> s)> p = boost::bind(
        &is_joined<cow_ptr_t<ns_metadata_t > >,
        _1,
        sl_metadata.rdb_namespaces
    );

    {
        on_thread_t switcher(namespaces_semilattice_metadata->home_thread());
        namespaces_semilattice_metadata->run_until_satisfied(p,
                                                             interruptor);
        databases_semilattice_metadata->run_until_satisfied(
            boost::bind(&is_joined<databases_semilattice_metadata_t>,
                        _1,
                        sl_metadata.databases),
            interruptor);
    }
}

boost::shared_ptr<js::runner_t> env_t::get_js_runner() {
    r_sanity_check(pool != NULL && get_thread_id() == pool->home_thread());
    if (!js_runner->connected()) {
        js_runner->begin(pool);
    }
    return js_runner;
}

env_t::env_t(
    extproc::pool_group_t *_pool_group,
    base_namespace_repo_t<rdb_protocol_t> *_ns_repo,

    clone_ptr_t<watchable_t<cow_ptr_t<ns_metadata_t> > >
    _namespaces_semilattice_metadata,

    clone_ptr_t<watchable_t<databases_semilattice_metadata_t> >
    _databases_semilattice_metadata,
    boost::shared_ptr<semilattice_readwrite_view_t<cluster_semilattice_metadata_t> >
    _semilattice_metadata,
    directory_read_manager_t<cluster_directory_metadata_t> *_directory_read_manager,
    boost::shared_ptr<js::runner_t> _js_runner,
    signal_t *_interruptor,
    uuid_u _this_machine,
    const std::map<std::string, wire_func_t> &_optargs)
  : uuid(generate_uuid()),
    optargs(_optargs),
    next_gensym_val(-2),
    implicit_depth(0),
    pool(_pool_group->get()),
    ns_repo(_ns_repo),
    namespaces_semilattice_metadata(_namespaces_semilattice_metadata),
    databases_semilattice_metadata(_databases_semilattice_metadata),
    semilattice_metadata(_semilattice_metadata),
    directory_read_manager(_directory_read_manager),
    js_runner(_js_runner),
    DEBUG_ONLY(eval_callback(NULL),)
    interruptor(_interruptor),
    this_machine(_this_machine) {

    guarantee(js_runner);
    bags.push_back(new ptr_bag_t());

}

env_t::env_t(signal_t *_interruptor)
  : uuid(generate_uuid()),
    next_gensym_val(-2),
    implicit_depth(0),
    pool(NULL),
    ns_repo(NULL),
    directory_read_manager(NULL),
    DEBUG_ONLY(eval_callback(NULL),)
    interruptor(_interruptor)
{
    bags.push_back(new ptr_bag_t());
}

env_t::~env_t() {
    guarantee(bags.size() == 1);
    delete bags[0];
}

} // namespace ql
