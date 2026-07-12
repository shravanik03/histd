/**
 * src/query_engine.cpp
 *
 * Implementation of QueryEngine.
 * See query_engine.hpp for design documentation.
 */
#include "query_engine.hpp"

#include <ctime>  // std::time

QueryEngine::QueryEngine(const std::string& records_path) : store_(records_path, Mode::READ) {}

void QueryEngine::ensure_index() {
    if (!index_built_) {
        index_.build(store_, tokenizer_);
        index_built_ = true;
    }
}

size_t QueryEngine::record_count() const { return store_.count(); }

std::vector<SearchResult> QueryEngine::search(const std::string& query, bool failed_only,
                                              uint32_t since_days, const std::string& project,
                                              size_t top_k) {
    ensure_index();
    auto results = index_.search(query, tokenizer_, top_k * 3);
    if (results.empty()) return {};
    auto filtered = apply_filters(results, failed_only, since_days, project);

    if (filtered.size() > top_k) {
        filtered.erase(filtered.begin() + top_k, filtered.end());
    }

    return filtered;
}

std::vector<SearchResult> QueryEngine::apply_filters(
    const std::vector<std::pair<uint64_t, float>>& results, bool failed_only, uint32_t since_days,
    const std::string& project) const {
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    uint64_t cutoff = since_days > 0 ? now - (since_days * 86400ULL) : 0;

    std::vector<SearchResult> filtered;

    for (auto& [offset, score] : results) {
        auto rec = store_.read_at(offset);

        if (rec.cmd.empty()) continue;
        if (failed_only && rec.exit_code == 0) continue;
        if (since_days > 0 && rec.timestamp < cutoff) continue;
        if (!project.empty() && rec.cwd.find(project) != 0) continue;

        filtered.push_back(SearchResult(rec, score, offset));
    }

    return filtered;
}
