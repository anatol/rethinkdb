// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef CLUSTERING_ADMINISTRATION_MAIN_IMPORT_HPP_
#define CLUSTERING_ADMINISTRATION_MAIN_IMPORT_HPP_

#include <set>
#include <string>

#include "errors.hpp"
#include <boost/optional.hpp>

#include "containers/name_string.hpp"
#include "arch/address.hpp"

class peer_address_set_t;
class json_importer_t;
class signal_t;

struct json_import_target_t {
    name_string_t db_name;
    boost::optional<name_string_t> datacenter_name;
    name_string_t table_name;
    std::string primary_key;
};

namespace extproc { class spawner_info_t; }

bool run_json_import(extproc::spawner_info_t *spawner_info,
                     peer_address_set_t peers,
                     const std::set<ip_address_t> &local_addresses,
                     int ports_port,
                     int ports_client_port,
                     json_import_target_t import_args,
                     json_importer_t *importer,
                     signal_t *stop_cond);




#endif  // CLUSTERING_ADMINISTRATION_MAIN_IMPORT_HPP_
