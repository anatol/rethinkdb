// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef RDB_PROTOCOL_PROTO_UTILS_HPP_
#define RDB_PROTOCOL_PROTO_UTILS_HPP_

#include <string>

#include "btree/keys.hpp"
#include "http/json.hpp"
#include "rdb_protocol/bt.hpp"
#include "rdb_protocol/exceptions.hpp"
#include "utils.hpp"

std::string cJSON_print_primary(cJSON *json, const query_language::backtrace_t &backtrace);

#ifndef NDEBUG
#define guarantee_debug_throw_release(cond, backtrace) guarantee(cond)
#else
#define guarantee_debug_throw_release(cond, backtrace) do {             \
        if (!(cond)) {                                                  \
            throw ::query_language::runtime_exc_t(format_assert_message("BROKEN SERVER GUARANTEE", cond), backtrace); \
        }                                                               \
    } while (0)
#endif // NDEBUG



#endif // RDB_PROTOCOL_PROTO_UTILS_HPP_

