// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef CLUSTERING_ADMINISTRATION_MAIN_JSON_IMPORT_HPP_
#define CLUSTERING_ADMINISTRATION_MAIN_JSON_IMPORT_HPP_

#include <string>
#include <vector>

#include "errors.hpp"

class bitset_t;
class scoped_cJSON_t;

class json_importer_t {
public:
    // Returns false upon EOF.
    virtual bool next_json(scoped_cJSON_t *out) = 0;

    // Returns true if we can't rule out that key as an acceptable primary key.
    virtual bool might_support_primary_key(const std::string& primary_key) = 0;

    virtual std::string get_error_information() const = 0;

    virtual ~json_importer_t() { }
};

class csv_to_json_importer_t : public json_importer_t {
public:
    csv_to_json_importer_t(std::string separators, std::string filepath);

    // Imports CSV given file contents, pre-split into lines.  For unit tests.
    csv_to_json_importer_t(std::string separators, std::vector<std::string> lines);

    // Returns false upon EOF.
    bool next_json(scoped_cJSON_t *out);

    bool might_support_primary_key(const std::string& primary_key);

    std::string get_error_information() const;

private:
    void import_json_from_file(std::string separators, std::string filepath);
    void import_json_from_lines(std::string separators, std::vector<std::string> *lines);

    std::vector<std::string> column_names_;
    std::vector<std::vector<std::string> > rows_;

    size_t position_;

    int64_t num_ignored_rows_;

    DISABLE_COPYING(csv_to_json_importer_t);
};

// For unit tests.
void separators_to_bitset(const std::string &separators, bitset_t *out);
std::vector<std::string> split_buf(const bitset_t &seps, const char *buf, int64_t size);

#endif  // CLUSTERING_ADMINISTRATION_MAIN_JSON_IMPORT_HPP_
