/**
 * src/failure_clusterer.hpp
 *
 * FailureClusterer — identifies commands that repeatedly fail.
 *
 * Scans all records in RecordStore, filters for non-zero exit codes,
 * normalizes command strings to group similar invocations together,
 * and returns the most frequently failing command clusters.
 *
 * Normalization replaces variable parts so that semantically identical
 * commands are grouped even if arguments differ:
 *   docker run -p 3000 myapp  →  docker run -p PORT myapp
 *   git push origin fix/123   →  git push origin fix/NUM
 *   psql -U admin mydb        →  psql -U admin mydb  (unchanged)
 *
 * This is the key differentiator from simple history tools — instead
 * of showing individual failures, it surfaces patterns of failure
 * that indicate commands worth investigating or fixing.
 *
 * Usage:
 *   RecordStore store(path, Mode::READ);
 *   FailureClusterer clusterer;
 *   auto groups = clusterer.cluster(store, 10);
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "record_store.hpp"

struct FailureGroup {
    std::string normalized_cmd;     // the command with variables stripped
    std::vector<std::string> dirs;  // unique directories the command failed in
    uint32_t count;                 // how many times it failed
    uint64_t last_seen;             // timestamp of most recent failure
    int32_t exit_code;              // most common exit code
};

class FailureClusterer {
   public:
    // scans store and returns top_n most frequently failing commands
    std::vector<FailureGroup> cluster(RecordStore& store, size_t top_n = 10);

   private:
    // returns true if the given token is a number, port
    bool is_numeric(const std::string& token) const;

    // normalizes a command string for grouping
    // replaces numbers, ports, and quoted strings with placeholders
    std::string normalize(const std::string& cmd) const;
};
