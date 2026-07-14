/**
 * src/query_engine.cpp
 *
 * Implementation of QueryEngine.
 * See query_engine.hpp for design documentation.
 */
#include "query_engine.hpp"

#include <cmath>  // std::log
#include <ctime>  // std::time

QueryEngine::QueryEngine(const std::string& records_path) : store_(records_path, Mode::READ) {}

void QueryEngine::ensure_index() {
    if (!index_built_) {
        index_.build(store_, tokenizer_);
        index_built_ = true;
    }
}

size_t QueryEngine::record_count() const { return store_.count(); }

size_t QueryEngine::term_count() {
    ensure_index();
    return index_.term_count();
}

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

std::vector<std::pair<uint64_t, float>> QueryEngine::search_via_reader(const std::string& query,
                                                                       size_t top_k) const {
    auto tokens = tokenizer_.tokenize(query);
    if (tokens.empty()) return {};

    std::vector<std::vector<PostingView>> posting_lists;
    std::vector<uint32_t> doc_counts;

    for (auto& token : tokens) {
        const DiskPosting* ptr = nullptr;
        uint32_t count = 0;
        if (!reader_->find_postings(token, &ptr, &count)) {
            return {};  // AND logic
        }

        std::vector<PostingView> views;
        views.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            views.push_back({ptr[i].byte_offset, ptr[i].score, ptr[i].timestamp});
        }
        posting_lists.push_back(std::move(views));
        doc_counts.push_back(count);
    }

    if (posting_lists.empty()) return {};

    // intersect all lists using shared logic
    std::vector<PostingView> result = posting_lists[0];
    for (size_t i = 1; i < posting_lists.size(); i++) {
        result = intersect_views(result, posting_lists[i]);
    }

    if (result.empty()) return {};

    // idf computed from reader's term stats instead of InvertedIndex
    float base_score = 0.0f;
    uint64_t total_records = reader_->total_records();
    for (auto count : doc_counts) {
        base_score += std::log(static_cast<float>(total_records + 1) / static_cast<float>(count));
    }

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    return rank_top_k(result, base_score, now, top_k);
}
