// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "rdb_protocol/val.hpp"

#include "rdb_protocol/env.hpp"
#include "rdb_protocol/meta_utils.hpp"
#include "rdb_protocol/pb_utils.hpp"
#include "rdb_protocol/term.hpp"

#pragma GCC diagnostic ignored "-Wshadow"

namespace ql {

// Most of this logic is copy-pasted from the old query language.
table_t::table_t(env_t *_env, uuid_u db_id, const std::string &name,
                 bool _use_outdated, const pb_rcheckable_t *src)
    : pb_rcheckable_t(src), env(_env), use_outdated(_use_outdated) {
    name_string_t table_name;
    bool b = table_name.assign_value(name);
    rcheck(b, strprintf("Table name `%s` invalid (%s).",
                        name.c_str(), valid_char_msg));
    cow_ptr_t<namespaces_semilattice_metadata_t<rdb_protocol_t> >
        namespaces_metadata = env->namespaces_semilattice_metadata->get();
    cow_ptr_t<namespaces_semilattice_metadata_t<rdb_protocol_t> >::change_t
        namespaces_metadata_change(&namespaces_metadata);
    metadata_searcher_t<namespace_semilattice_metadata_t<rdb_protocol_t> >
        ns_searcher(&namespaces_metadata_change.get()->namespaces);
    //TODO: fold into iteration below
    namespace_predicate_t pred(&table_name, &db_id);
    uuid_u id = meta_get_uuid(&ns_searcher, pred,
                              strprintf("Table `%s` does not exist.",
                                        table_name.c_str()), this);

    access.init(new namespace_repo_t<rdb_protocol_t>::access_t(
                    env->ns_repo, id, env->interruptor));

    metadata_search_status_t status;
    metadata_searcher_t<namespace_semilattice_metadata_t<rdb_protocol_t> >::iterator
        ns_metadata_it = ns_searcher.find_uniq(pred, &status);
    rcheck(status == METADATA_SUCCESS,
           strprintf("Table `%s` does not exist.", table_name.c_str()));
    guarantee(!ns_metadata_it->second.is_deleted());
    r_sanity_check(!ns_metadata_it->second.get().primary_key.in_conflict());
    pkey =  ns_metadata_it->second.get().primary_key.get();
}

datum_t *table_t::env_add_ptr(datum_t *d) {
    return env->add_ptr(d);
}

const datum_t *table_t::make_error_datum(const base_exc_t &exception) {
    datum_t *datum = env_add_ptr(new datum_t(datum_t::R_OBJECT));
    const std::string err = exception.what();

    // The bool is true if there's a conflict when inserting the
    // key, but since we just created an empty object above conflicts
    // are impossible here.  If you want to harden this against future
    // changes, you could store the bool and `r_sanity_check` that it's
    // false.
    DEBUG_VAR const bool had_first_error =
        datum->add("first_error", env_add_ptr(new datum_t(err)));
    rassert(!had_first_error);

    DEBUG_VAR const bool had_errors =
        datum->add("errors", env_add_ptr(new datum_t(1.0)));
    rassert(!had_errors);

    return datum;
}

const datum_t *table_t::replace(const datum_t *original,
                                func_t *replacement_generator,
                                bool nondet_ok) {
    try {
        return do_replace(original, replacement_generator, nondet_ok);
    } catch (const base_exc_t &exc) {
        return make_error_datum(exc);
    }
}

const datum_t *table_t::replace(const datum_t *original,
                                const datum_t *replacement,
                                bool upsert) {
    try {
        return do_replace(original, replacement, upsert);
    } catch (const base_exc_t &exc) {
        return make_error_datum(exc);
    }
}


std::vector<const datum_t *> table_t::batch_replace(
    const std::vector<const datum_t *> &original_values,
    func_t *replacement_generator,
    const bool nondeterministic_replacements_ok) {
    if (replacement_generator->is_deterministic()) {
        std::vector<datum_func_pair_t> pairs(original_values.size());
        map_wire_func_t wire_func = map_wire_func_t(env, replacement_generator);
        for (size_t i = 0; i < original_values.size(); ++i) {
            try {
                pairs[i] = datum_func_pair_t(original_values[i], &wire_func);
            } catch (const base_exc_t &exc) {
                pairs[i] = datum_func_pair_t(make_error_datum(exc));
            }
        }

        return batch_replace(pairs);
    } else {
        r_sanity_check(nondeterministic_replacements_ok);

        scoped_array_t<map_wire_func_t> funcs(original_values.size());
        std::vector<datum_func_pair_t> pairs(original_values.size());
        for (size_t i = 0; i < original_values.size(); ++i) {
            try {
                const datum_t *replacement =
                    replacement_generator->call(original_values[i])->as_datum();

                Term t;
                const int x = env->gensym();
                Term *const arg = pb::set_func(&t, x);
                replacement->write_to_protobuf(pb::set_datum(arg));
                propagate(&t);

                funcs[i] = map_wire_func_t(
                    t, static_cast<std::map<int64_t, Datum> *>(NULL));
                pairs[i] = datum_func_pair_t(original_values[i], &funcs[i]);
            } catch (const base_exc_t &exc) {
                pairs[i] = datum_func_pair_t(make_error_datum(exc));
            }
        }

        return batch_replace(pairs);
    }
}

std::vector<const datum_t *> table_t::batch_replace(
    const std::vector<const datum_t *> &original_values,
    const std::vector<const datum_t *> &replacement_values,
    bool upsert) {
    r_sanity_check(original_values.size() == replacement_values.size());
    scoped_array_t<map_wire_func_t> funcs(original_values.size());
    std::vector<datum_func_pair_t> pairs(original_values.size());
    for (size_t i = 0; i < original_values.size(); ++i) {
        try {
            Term t;
            const int x = env->gensym();
            Term *const arg = pb::set_func(&t, x);
            if (upsert) {
                replacement_values[i]->write_to_protobuf(pb::set_datum(arg));
            } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
                N3(BRANCH,
                   N2(EQ, NVAR(x), NDATUM(datum_t::R_NULL)),
                   NDATUM(replacement_values[i]),
                   N1(ERROR, NDATUM("Duplicate primary key.")));
#pragma GCC diagnostic pop
            }

            propagate(&t);
            funcs[i] = map_wire_func_t(t, static_cast<std::map<int64_t, Datum> *>(NULL));
            pairs[i] = datum_func_pair_t(original_values[i], &funcs[i]);
        } catch (const base_exc_t &exc) {
            pairs[i] = datum_func_pair_t(make_error_datum(exc));
        }
    }

    return batch_replace(pairs);
}

bool is_sorted_by_first(const std::vector<std::pair<int64_t, Datum> > &v) {
    if (v.size() == 0) {
        return true;
    }

    auto it = v.begin();
    auto jt = it + 1;
    while (jt < v.end()) {
        if (!(it->first < jt->first)) {
            return false;
        }
        ++it;
        ++jt;
    }
    return true;
}


std::vector<const datum_t *> table_t::batch_replace(
    const std::vector<datum_func_pair_t> &replacements) {
    std::vector<const datum_t *> ret(replacements.size(), NULL);

    std::vector<std::pair<int64_t, rdb_protocol_t::point_replace_t> > point_replaces;

    for (size_t i = 0; i < replacements.size(); ++i) {
        try {
            if (replacements[i].error_value != NULL) {
                r_sanity_check(replacements[i].original_value == NULL);
                ret[i] = replacements[i].error_value;
            } else {
                const datum_t *orig = replacements[i].original_value;
                r_sanity_check(orig != NULL);

                if (orig->get_type() == datum_t::R_NULL) {
                    // TODO: We copy this for some reason, possibly no reason.
                    map_wire_func_t mwf = *replacements[i].replacer;
                    orig = mwf.compile(env)->call(orig)->as_datum();
                    if (orig->get_type() == datum_t::R_NULL) {
                        datum_t *resp = env->add_ptr(new datum_t(datum_t::R_OBJECT));
                        const datum_t *const one = env->add_ptr(new ql::datum_t(1.0));
                        const bool b = resp->add("skipped", one);
                        r_sanity_check(!b);
                        ret[i] = resp;
                        continue;
                    }
                }

                const std::string &pk = get_pkey();
                store_key_t store_key(orig->get(pk)->print_primary());
                point_replaces.push_back(
                    std::make_pair(static_cast<int64_t>(point_replaces.size()),
                                   rdb_protocol_t::point_replace_t(
                                       pk, store_key,
                                       *replacements[i].replacer,
                                       env->get_all_optargs())));
            }
        } catch (const base_exc_t& exc) {
            ret[i] = make_error_datum(exc);
        }
    }

    rdb_protocol_t::write_t write((rdb_protocol_t::batched_replaces_t(point_replaces)));
    rdb_protocol_t::write_response_t response;
    access->get_namespace_if()->write(write, &response, order_token_t::ignore, env->interruptor);
    rdb_protocol_t::batched_replaces_response_t *batched_replaces_response
        = boost::get<rdb_protocol_t::batched_replaces_response_t>(&response.response);
    r_sanity_check(batched_replaces_response != NULL);
    std::vector<std::pair<int64_t, Datum> > *datums = &batched_replaces_response->point_replace_responses;

    rassert(is_sorted_by_first(*datums));

    size_t j = 0;
    for (size_t i = 0; i < ret.size(); ++i) {
        if (ret[i] == NULL) {
            r_sanity_check(j < datums->size());
            ret[i] = env->add_ptr(new datum_t(&(*datums)[j].second, env));
            ++j;
        }
    }
    r_sanity_check(j == datums->size());

    return ret;
}

MUST_USE bool table_t::sindex_create(const std::string &id, func_t *index_func) {
    index_func->assert_deterministic("Index functions must be deterministic.");
    map_wire_func_t wire_func(env, index_func);
    rdb_protocol_t::write_t write(
            rdb_protocol_t::sindex_create_t(id, wire_func));

    rdb_protocol_t::write_response_t res;
    access->get_namespace_if()->write(
        write, &res, order_token_t::ignore, env->interruptor);

    rdb_protocol_t::sindex_create_response_t *response =
        boost::get<rdb_protocol_t::sindex_create_response_t>(&res.response);
    r_sanity_check(response);
    return response->success;
}

MUST_USE bool table_t::sindex_drop(const std::string &id) {
    rdb_protocol_t::write_t write((
            rdb_protocol_t::sindex_drop_t(id)));

    rdb_protocol_t::write_response_t res;
    access->get_namespace_if()->write(
        write, &res, order_token_t::ignore, env->interruptor);

    rdb_protocol_t::sindex_drop_response_t *response =
        boost::get<rdb_protocol_t::sindex_drop_response_t>(&res.response);
    r_sanity_check(response);
    return response->success;
}

const datum_t *table_t::sindex_list() {
    datum_t *array = env->add_ptr(new datum_t(datum_t::R_ARRAY));
    rdb_protocol_t::sindex_list_t sindex_list;
    rdb_protocol_t::read_t read(sindex_list);
    try {
        rdb_protocol_t::read_response_t res;
        access->get_namespace_if()->read(read, &res, order_token_t::ignore, env->interruptor);
        rdb_protocol_t::sindex_list_response_t *s_res = boost::get<rdb_protocol_t::sindex_list_response_t>(&res.response);
        r_sanity_check(s_res);

        for (auto it = s_res->sindexes.begin(); it != s_res->sindexes.end(); ++it) {
            array->add(env->add_ptr(new datum_t(*it)));
        }
    } catch (const cannot_perform_query_exc_t &ex) {
        rfail("cannot perform read: %s", ex.what());
    }

    return array;
}

const datum_t *table_t::do_replace(const datum_t *orig, const map_wire_func_t &mwf) {
    const std::string &pk = get_pkey();
    if (orig->get_type() == datum_t::R_NULL) {
        map_wire_func_t mwf2 = mwf;
        orig = mwf2.compile(env)->call(orig)->as_datum();
        if (orig->get_type() == datum_t::R_NULL) {
            datum_t *resp = env->add_ptr(new datum_t(datum_t::R_OBJECT));
            const datum_t *num_1 = env->add_ptr(new ql::datum_t(1.0));
            bool b = resp->add("skipped", num_1);
            r_sanity_check(!b);
            return resp;
        }
    }
    store_key_t store_key(orig->get(pk)->print_primary());
    rdb_protocol_t::write_t write(
        rdb_protocol_t::point_replace_t(pk, store_key, mwf, env->get_all_optargs()));

    rdb_protocol_t::write_response_t response;
    access->get_namespace_if()->write(
        write, &response, order_token_t::ignore, env->interruptor);
    Datum *d = boost::get<Datum>(&response.response);
    return env->add_ptr(new datum_t(d, env));
}

const datum_t *table_t::do_replace(const datum_t *orig, func_t *f, bool nondet_ok) {
    if (f->is_deterministic()) {
        return do_replace(orig, map_wire_func_t(env, f));
    } else {
        r_sanity_check(nondet_ok);
        return do_replace(orig, f->call(orig)->as_datum(), true);
    }
}

const datum_t *table_t::do_replace(const datum_t *orig, const datum_t *d, bool upsert) {
    Term t;
    int x = env->gensym();
    Term *arg = pb::set_func(&t, x);
    if (upsert) {
        d->write_to_protobuf(pb::set_datum(arg));
    } else {
        N3(BRANCH,
           N2(EQ, NVAR(x), NDATUM(datum_t::R_NULL)),
           NDATUM(d),
           N1(ERROR, NDATUM("Duplicate primary key.")));
    }

    propagate(&t);
    return do_replace(
        orig, map_wire_func_t(t, static_cast<std::map<int64_t, Datum> *>(NULL)));
}

const std::string &table_t::get_pkey() { return pkey; }

const datum_t *table_t::get_row(const datum_t *pval) {
    std::string pks = pval->print_primary();
    rdb_protocol_t::read_t read((rdb_protocol_t::point_read_t(store_key_t(pks))));
    rdb_protocol_t::read_response_t res;
    if (use_outdated) {
        access->get_namespace_if()->read_outdated(read, &res, env->interruptor);
    } else {
        access->get_namespace_if()->read(
            read, &res, order_token_t::ignore, env->interruptor);
    }
    rdb_protocol_t::point_read_response_t *p_res =
        boost::get<rdb_protocol_t::point_read_response_t>(&res.response);
    r_sanity_check(p_res);
    return env->add_ptr(new datum_t(p_res->data, env));
}

datum_stream_t *table_t::get_sindex_rows(const datum_t *pval,
                                         const std::string &sindex_id,
                                         const pb_rcheckable_t *bt) {
    return env->add_ptr(
            new lazy_datum_stream_t(env, use_outdated, access.get(),
                                    pval, sindex_id, bt));
}

datum_stream_t *table_t::as_datum_stream() {
    return env->add_ptr(
        new lazy_datum_stream_t(env, use_outdated, access.get(), this));
}

val_t::type_t::type_t(val_t::type_t::raw_type_t _raw_type) : raw_type(_raw_type) { }

// NOTE: This *MUST* be kept in sync with the surrounding code (not that it
// should have to change very often).
bool raw_type_is_convertible(val_t::type_t::raw_type_t _t1,
                             val_t::type_t::raw_type_t _t2) {
    const int t1 = _t1, t2 = _t2,
        DB = val_t::type_t::DB,
        TABLE = val_t::type_t::TABLE,
        SELECTION = val_t::type_t::SELECTION,
        SEQUENCE = val_t::type_t::SEQUENCE,
        SINGLE_SELECTION = val_t::type_t::SINGLE_SELECTION,
        DATUM = val_t::type_t::DATUM,
        FUNC = val_t::type_t::FUNC;
    switch (t1) {
    case DB: return t2 == DB;
    case TABLE: return t2 == TABLE || t2 == SELECTION || t2 == SEQUENCE;
    case SELECTION: return t2 == SELECTION || t2 == SEQUENCE;
    case SEQUENCE: return t2 == SEQUENCE;
    case SINGLE_SELECTION: return t2 == SINGLE_SELECTION || t2 == DATUM;
    case DATUM: return t2 == DATUM || t2 == SEQUENCE;
    case FUNC: return t2 == FUNC;
    default: unreachable();
    }
    unreachable();
}
bool val_t::type_t::is_convertible(type_t rhs) const {
    return raw_type_is_convertible(raw_type, rhs.raw_type);
}

const char *val_t::type_t::name() const {
    switch (raw_type) {
    case DB: return "DATABASE";
    case TABLE: return "TABLE";
    case SELECTION: return "SELECTION";
    case SEQUENCE: return "SEQUENCE";
    case SINGLE_SELECTION: return "SINGLE_SELECTION";
    case DATUM: return "DATUM";
    case FUNC: return "FUNCTION";
    default: unreachable();
    }
    unreachable();
}

val_t::val_t(const datum_t *_datum, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::DATUM),
      table(NULL),
      datum(get_env()->add_ptr(_datum)) {
    guarantee(table == NULL);
    guarantee(datum != NULL);
}
val_t::val_t(const datum_t *_datum, table_t *_table, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::SINGLE_SELECTION),
      table(get_env()->add_ptr(_table)),
      datum(_datum) {
    guarantee(table != NULL);
    guarantee(datum != NULL);
}
val_t::val_t(datum_stream_t *_sequence, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::SEQUENCE),
      table(NULL),
      sequence(get_env()->add_ptr(_sequence)) {
    guarantee(table == NULL);
    guarantee(sequence != NULL);
    // Some streams are really arrays in disguise.
    const datum_t *arr = sequence->as_array();
    if (arr) {
        type = type_t::DATUM;
        datum = arr;
    }
}

val_t::val_t(table_t *_table, datum_stream_t *_sequence, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::SELECTION),
      table(get_env()->add_ptr(_table)),
      sequence(get_env()->add_ptr(_sequence)) {
    guarantee(table != NULL);
    guarantee(sequence != NULL);
}

val_t::val_t(table_t *_table, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::TABLE),
      table(get_env()->add_ptr(_table)),
      sequence(NULL) {
    guarantee(table != NULL);
    guarantee(sequence == NULL);
}
val_t::val_t(uuid_u _db, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::DB),
      table(NULL) {
    guarantee(table == NULL);
    *db_ptr() = _db;
}
val_t::val_t(func_t *_func, const term_t *_parent)
    : pb_rcheckable_t(_parent),
      parent(_parent),
      type(type_t::FUNC),
      table(NULL),
      func(get_env()->add_ptr(_func)) {
    guarantee(table == NULL);
    guarantee(func != NULL);
}

val_t::~val_t() {
    if (get_type().is_convertible(type_t::DB)) {
        // This isn't necessary right now because the `uuid_u` destructor
        // doesn't do anything useful, but it helps make us future-proof.
        db_ptr()->~uuid_u();
    }
}

val_t::type_t val_t::get_type() const { return type; }
const char * val_t::get_type_name() const { return get_type().name(); }

const datum_t *val_t::as_datum() {
    if (type.raw_type != type_t::DATUM && type.raw_type != type_t::SINGLE_SELECTION) {
        rcheck_literal_type(type_t::DATUM);
    }
    return datum;
}

table_t *val_t::as_table() {
    rcheck_literal_type(type_t::TABLE);
    return table;
}

datum_stream_t *val_t::as_seq() {
    if (type.raw_type == type_t::SEQUENCE || type.raw_type == type_t::SELECTION) {
        return sequence;
    } else if (type.raw_type == type_t::TABLE) {
        return table->as_datum_stream();
    } else if (type.raw_type == type_t::DATUM) {
        return datum->as_datum_stream(get_env(), parent);
    }
    rcheck_literal_type(type_t::SEQUENCE);
    unreachable();
}

std::pair<table_t *, datum_stream_t *> val_t::as_selection() {
    if (type.raw_type != type_t::TABLE && type.raw_type != type_t::SELECTION) {
        rcheck_literal_type(type_t::SELECTION);
    }
    return std::make_pair(table, as_seq());
}

std::pair<table_t *, const datum_t *> val_t::as_single_selection() {
    rcheck_literal_type(type_t::SINGLE_SELECTION);
    return std::make_pair(table, datum);
}

func_t *val_t::as_func(function_shortcut_t shortcut) {
    if (get_type().is_convertible(type_t::FUNC)) {
        r_sanity_check(func);
        return func;
    }

    if (shortcut == NO_SHORTCUT) {
        rcheck_literal_type(type_t::FUNC);
        unreachable();
    }

    if (!type.is_convertible(type_t::DATUM)) {
        rcheck_literal_type(type_t::FUNC);
        unreachable();
    }

    r_sanity_check(parent);
    // We use a switch here so that people have to update it if they add another
    // shortcut.
    switch(shortcut) {
    case IDENTITY_SHORTCUT: {
        return get_env()->add_ptr(
            func_t::new_identity_func(get_env(), as_datum(), parent));
    } break;
    case NO_SHORTCUT: //fallthru
    default: unreachable();
    }
    unreachable();
}

uuid_u val_t::as_db() {
    rcheck_literal_type(type_t::DB);
    return *db_ptr();
}

bool val_t::as_bool() {
    try {
        const datum_t *d = as_datum();
        r_sanity_check(d);
        return d->as_bool();
    } catch (const datum_exc_t &e) {
        rfail("%s", e.what());
        unreachable();
    }
}
double val_t::as_num() {
    try {
        const datum_t *d = as_datum();
        r_sanity_check(d);
        return d->as_num();
    } catch (const datum_exc_t &e) {
        rfail("%s", e.what());
        unreachable();
    }
}
int64_t val_t::as_int() {
    try {
        const datum_t *d = as_datum();
        r_sanity_check(d);
        return d->as_int();
    } catch (const datum_exc_t &e) {
        rfail("%s", e.what());
        unreachable();
    }
}
const std::string &val_t::as_str() {
    try {
        const datum_t *d = as_datum();
        r_sanity_check(d);
        return d->as_str();
    } catch (const datum_exc_t &e) {
        rfail("%s", e.what());
        unreachable();
    }
}

void val_t::rcheck_literal_type(type_t::raw_type_t expected_raw_type) {
    rcheck(type.raw_type == expected_raw_type,
           strprintf("Expected type %s but found %s:\n%s",
                     type_t(expected_raw_type).name(), type.name(), print().c_str()));
}

} //namespace ql
