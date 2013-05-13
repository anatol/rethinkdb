#include "rdb_protocol/stream_cache.hpp"

#include "rdb_protocol/env.hpp"

namespace ql {

bool stream_cache2_t::contains(int64_t key) {
    return streams.find(key) != streams.end();
}

void stream_cache2_t::insert(int64_t key,
                             scoped_ptr_t<env_t> &&val_env,
                             counted_t<datum_stream_t> val_stream) {
    maybe_evict();
    std::pair<boost::ptr_map<int64_t, entry_t>::iterator, bool> res =
        streams.insert(key, new entry_t(time(0), std::move(val_env), val_stream));
    guarantee(res.second);
}

void stream_cache2_t::erase(int64_t key) {
    size_t num_erased = streams.erase(key);
    guarantee(num_erased == 1);
}

bool stream_cache2_t::serve(int64_t key, Response *res, signal_t *interruptor) {
    boost::ptr_map<int64_t, entry_t>::iterator it = streams.find(key);
    if (it == streams.end()) {
        return false;
    }

    entry_t *entry = it->second;
    entry->last_activity = time(0);
    try {
        // This is a hack.  Some streams have an interruptor that is invalid by
        // the time we reach here, so we just reset it to a good one.
        entry->env->interruptor = interruptor;

        int chunk_size = 0;
        if (entry->next_datum.has()) {
            *res->add_response() = *entry->next_datum.get();
            ++chunk_size;
            entry->next_datum.reset();
        }
        rassert(entry->max_chunk_size > 0);

        // We add 1 to allow for the "next_datum" value that the javascript
        // driver currently needs us to hoard.  (Note that the argument to
        // next_batch must be positive.)
        std::vector<counted_t<const datum_t> > datums
            = entry->stream->next_batch(entry->max_chunk_size + 1 - chunk_size);

        if (datums.size() > 0) {
            counted_t<const datum_t> last = std::move(datums.back());
            datums.pop_back();

            entry->next_datum.init(new Datum());
            last->write_to_protobuf(entry->next_datum.get());

            for (auto datum = datums.begin(); datum != datums.end(); ++datum) {
                (*datum)->write_to_protobuf(res->add_response());
            }

            res->set_type(Response::SUCCESS_PARTIAL);
        } else {
            erase(key);
            res->set_type(Response::SUCCESS_SEQUENCE);
        }

        return true;
    } catch (const std::exception &e) {
        erase(key);
        throw;
    }
}

void stream_cache2_t::maybe_evict() {
    // We never evict right now.
}

stream_cache2_t::entry_t::entry_t(time_t _last_activity, scoped_ptr_t<env_t> &&env_ptr,
                                  counted_t<datum_stream_t> _stream)
    : last_activity(_last_activity), env(std::move(env_ptr)), stream(_stream),
      max_chunk_size(DEFAULT_MAX_CHUNK_SIZE), max_age(DEFAULT_MAX_AGE) { }

stream_cache2_t::entry_t::~entry_t() { }


} // namespace ql
