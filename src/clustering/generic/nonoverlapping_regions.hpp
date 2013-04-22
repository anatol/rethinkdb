// Copyright 2010-2013 RethinkDB, all rights reserved.
#ifndef CLUSTERING_GENERIC_NONOVERLAPPING_REGIONS_HPP_
#define CLUSTERING_GENERIC_NONOVERLAPPING_REGIONS_HPP_

#include <set>
#include <vector>

#include "errors.hpp"
#include "http/json/json_adapter.hpp"
#include "rpc/serialize_macros.hpp"

template <class protocol_t>
class nonoverlapping_regions_t {
public:
    nonoverlapping_regions_t() { }

    // Returns true upon success, false if there's an overlap or the
    // regions don't join to the universal set.  This is like calling
    // add_region in a loop and then valid_for_sharding, only maybe without being O(n^2).
    MUST_USE bool set_regions(const std::vector<typename protocol_t::region_t> &regions);

    // We already know by the public interface that no regions
    // overlap.  This checks that their join is the universe, and that
    // there is no empty region.
    bool valid_for_sharding() const;

    // TODO: Use valid_for_sharding everywhere we use add_region.

    // Returns true upon success, false if there's an overlap.  This is O(n)!
    MUST_USE bool add_region(const typename protocol_t::region_t &region);

    // Yes, both of these are const iterators.
    typedef typename std::set<typename protocol_t::region_t>::iterator iterator;
    iterator begin() const { return regions_.begin(); }
    iterator end() const { return regions_.end(); }

    bool empty() const { return regions_.empty(); }
    size_t size() const { return regions_.size(); }

    void remove_region(iterator it) {
        guarantee(it != regions_.end());
        regions_.erase(it);
    }

    bool operator==(const nonoverlapping_regions_t<protocol_t> &other) const {
        return regions_ == other.regions_;
    }

    RDB_MAKE_ME_SERIALIZABLE_1(regions_);

private:
    std::set<typename protocol_t::region_t> regions_;
};

// ctx-less json adapter concept for nonoverlapping_regions_t.
template <class protocol_t>
json_adapter_if_t::json_adapter_map_t get_json_subfields(nonoverlapping_regions_t<protocol_t> *target);

template <class protocol_t>
cJSON *render_as_json(nonoverlapping_regions_t<protocol_t> *target);

template <class protocol_t>
void apply_json_to(cJSON *change, nonoverlapping_regions_t<protocol_t> *target) THROWS_ONLY(schema_mismatch_exc_t);



#endif  // CLUSTERING_GENERIC_NONOVERLAPPING_REGIONS_HPP_
