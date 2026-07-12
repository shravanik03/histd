/**
 * src/failure_clusterer.cpp
 *
 * Implementation of FailureClusterer.
 * See failure_clusterer.hpp for design documentation.
 */

#include "failure_clusterer.hpp"

#include <algorithm>      // std::sort, std::isdigit
#include <unordered_map>  // std::unordered_map
#include <unordered_set>  // std::unordered_set

std::vector<FailureGroup> FailureClusterer::cluster(RecordStore& store, size_t top_n) {
    std::unordered_map<std::string, FailureGroup> groups;

    store.for_each([&](const ParsedRecord& rec) {
        if (rec.exit_code == 0) return;
        std::string key = normalize(rec.cmd);
        if (groups.find(key) == groups.end()) {
            FailureGroup fg;
            fg.normalized_cmd = key;
            fg.count = 1;
            fg.last_seen = rec.timestamp;
            fg.exit_code = rec.exit_code;
            fg.dirs.push_back(rec.cwd);
            groups[key] = fg;
        } else {
            groups[key].count++;
            if (rec.timestamp > groups[key].last_seen) {
                groups[key].last_seen = rec.timestamp;
            }
            // add cwd to dirs if not already present
            if (std::find(groups[key].dirs.begin(), groups[key].dirs.end(), rec.cwd) ==
                groups[key].dirs.end()) {
                groups[key].dirs.push_back(rec.cwd);
            }
        }
    });

    std::vector<FailureGroup> result;
    for (auto& [key, fg] : groups) {
        result.push_back(fg);
    }

    std::sort(result.begin(), result.end(),
              [](const FailureGroup& a, const FailureGroup& b) { return a.count > b.count; });

    if (result.size() > top_n) {
        result.resize(top_n);
    }

    return result;
}

bool FailureClusterer::is_numeric(const std::string& token) const {
    if (token.empty()) return false;
    for (char c : token) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

std::string FailureClusterer::normalize(const std::string& cmd) const {
    std::unordered_set<char> delimiters = {' ', '-', '/', '.', '_', '=', ':', ','};
    std::string current_token;
    std::string normalized;

    for (size_t i = 0; i < cmd.size(); i++) {
        char c = cmd[i];
        if (delimiters.find(c) != delimiters.end()) {
            if (current_token.empty()) continue;
            if (is_numeric(current_token)) {
                int val = std::stoi(current_token);
                current_token = (val >= 1024 && val <= 65535) ? "PORT" : "NUM";
            } else if (current_token.front() == '"' || current_token.front() == '\'') {
                current_token = "STR";
            }
            if (!current_token.empty()) {
                if (!normalized.empty()) normalized += ' ';
                normalized += current_token;
                current_token.clear();
            }
            continue;
        }
        current_token += c;
    }

    // after the loop — handle last token
    if (!current_token.empty()) {
        if (is_numeric(current_token)) {
            int val = std::stoi(current_token);
            current_token = (val >= 1024 && val <= 65535) ? "PORT" : "NUM";
        } else if (current_token.front() == '"' || current_token.front() == '\'') {
            current_token = "STR";
        }
        if (!normalized.empty()) normalized += ' ';
        normalized += current_token;
    }

    return normalized;
}