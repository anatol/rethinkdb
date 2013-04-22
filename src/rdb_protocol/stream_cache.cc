#include "rdb_protocol/stream_cache.hpp"

#include "rdb_protocol/env.hpp"

namespace ql {

bool stream_cache2_t::contains(int64_t key) {
    return streams.find(key) != streams.end();
}

void stream_cache2_t::insert(Query *q, int64_t key,
                             scoped_ptr_t<env_t> *val_env,
                             datum_stream_t *val_stream) {
    maybe_evict();
    std::pair<boost::ptr_map<int64_t, entry_t>::iterator, bool> res =
        streams.insert(key, new entry_t(time(0), val_env, val_stream, q));
    guarantee(res.second);
}

void stream_cache2_t::erase(int64_t key) {
    size_t num_erased = streams.erase(key);
    guarantee(num_erased == 1);
}

bool stream_cache2_t::serve(int64_t key, Response *res, signal_t *interruptor) {
    boost::ptr_map<int64_t, entry_t>::iterator it = streams.find(key);
    if (it == streams.end()) return false;
    entry_t *entry = it->second;
    entry->last_activity = time(0);
    try {
        // This is a hack.  Some streams have an interruptor that is invalid by
        // the time we reach here, so we just reset it to a good one.
        entry->env->interruptor = interruptor;
        env_checkpoint_t(entry->env.get(), env_checkpoint_t::DISCARD);

        int chunk_size = 0;
        if (entry->next_datum.has()) {
            *res->add_response() = *entry->next_datum.get();
            ++chunk_size;
            entry->next_datum.reset();
        }
        while (const datum_t *d = entry->stream->next()) {
            d->write_to_protobuf(res->add_response());
            if (entry->max_chunk_size && ++chunk_size >= entry->max_chunk_size) {
                if (const datum_t *next_d = entry->stream->next()) {
                    r_sanity_check(!entry->next_datum.has());
                    entry->next_datum.init(new Datum());
                    next_d->write_to_protobuf(entry->next_datum.get());
                    res->set_type(Response::SUCCESS_PARTIAL);
                }
                break;
            }
        }
    } catch (const std::exception &e) {
        erase(key);
        throw;
    }
    if (!entry->next_datum.has()) {
        erase(key);
        res->set_type(Response::SUCCESS_SEQUENCE);
    } else {
        r_sanity_check(res->type() == Response::SUCCESS_PARTIAL);
    }
    return true;
}

void stream_cache2_t::maybe_evict() {
    // We never evict right now.
}

bool valid_chunk_size(int64_t chunk_size) {
    // TODO: when users can specify chunk sizes, pick a better bound than INT_MAX.
    return 0 <= chunk_size && chunk_size <= INT_MAX;
}
bool valid_age(int64_t age) { return 0 <= age; }

stream_cache2_t::entry_t::entry_t(time_t _last_activity, scoped_ptr_t<env_t> *env_ptr,
                                 datum_stream_t *_stream, UNUSED Query *q)
    : last_activity(_last_activity), env(env_ptr->release() /*!!!*/), stream(_stream),
      max_chunk_size(DEFAULT_MAX_CHUNK_SIZE), max_age(DEFAULT_MAX_AGE)
{
    // TODO: Once we actually support setting chunk size and age in the clients,
    // parse the possible non-default values out of `q` here.
}

stream_cache2_t::entry_t::~entry_t() { }


} // namespace ql
