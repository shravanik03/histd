/**
 * src/inverted_index.cpp
 *
 * Implementation of InvertedIndex.
 * See inverted_index.hpp for design documentation.
 */
#include "inverted_index.hpp"

#include <algorithm>      // std::sort, std::transform, std::max
#include <cmath>          // std::log
#include <ctime>          // std::time
#include <queue>          // std::priority_queue
#include <unordered_set>  // std::unordered_set
#include <vector>         // std::vector

void InvertedIndex::build(RecordStore& store, const Tokenizer& tokenizer) {
    index_.clear();
    total_records_ = store.count();

    uint64_t byte_offset = 0;

    store.for_each([&](const ParsedRecord& rec) {
        auto cmd_tokens = tokenizer.tokenize(rec.cmd);
        auto cwd_tokens = tokenizer.tokenize(rec.cwd);

        // combine cmd and cwd tokens
        auto all_tokens = cmd_tokens;
        all_tokens.insert(all_tokens.end(), cwd_tokens.begin(), cwd_tokens.end());

        std::unordered_set<std::string> seen;

        for (auto& token : all_tokens) {
            if (seen.find(token) != seen.end()) continue;
            seen.insert(token);
            index_[token].push_back(Posting(byte_offset, 1.0f, rec.timestamp));
        }
        byte_offset += sizeof(CommandRecord) + rec.cmd.size() + rec.cwd.size();
    });

    for (auto& entry : index_) {
        std::sort(entry.second.begin(), entry.second.end());
    }
}

std::vector<std::pair<uint64_t, float>> InvertedIndex::search(const std::string& query,
                                                              const Tokenizer& tokenizer,
                                                              size_t top_k) const {
    using ScoreId = std::pair<float, uint64_t>;
    // tokenize the query
    auto tokens = tokenizer.tokenize(query);
    if (tokens.empty()) return {};

    // get posting lists for each token
    std::vector<std::vector<Posting>> posting_lists;
    for (auto& token : tokens) {
        auto it = index_.find(token);
        if (it == index_.end()) return {};
        posting_lists.push_back(it->second);
    }

    if (posting_lists.empty()) return {};

    // intersect all posting lists
    std::vector<Posting> result = posting_lists[0];
    for (size_t i = 1; i < posting_lists.size(); i++) {
        result = intersect(result, posting_lists[i]);
    }

    if (result.empty()) return {};

    // score each posting inside result
    uint64_t now = std::time(nullptr);
    float base_score = 0.0f;
    for (auto& token : tokens) {
        base_score += idf(token);
    }

    for (auto& posting : result) {
        float days_ago = static_cast<float>(now - posting.timestamp) / 86400.0f;
        float recency_boost = 1.0f + 1.0f / (1.0f + days_ago);
        posting.score = base_score * recency_boost;
    }

    // find top_k records based on score
    std::priority_queue<ScoreId, std::vector<ScoreId>, std::greater<ScoreId>> pq;

    for (auto& posting : result) {
        pq.push({posting.score, posting.byte_offset});
        if (pq.size() > top_k) pq.pop();
    }

    std::vector<std::pair<uint64_t, float>> top_k_records;
    while (!pq.empty()) {
        top_k_records.push_back({pq.top().second, pq.top().first});
        pq.pop();
    }
    std::reverse(top_k_records.begin(), top_k_records.end());
    return top_k_records;
}

size_t InvertedIndex::term_count() const { return index_.size(); }

size_t InvertedIndex::record_count() const { return total_records_; }

float InvertedIndex::idf(const std::string& token) const {
    auto it = index_.find(token);
    if (it == index_.end()) return 0.0f;  // token not found

    size_t docs_with_token = it->second.size();
    if (docs_with_token == 0) return 0.0f;

    // IDF = log(total / docs_with_token)
    // add 1 to avoid division by zero if total_records_ == 0
    return std::log(static_cast<float>(total_records_ + 1) / static_cast<float>(docs_with_token));
}

std::vector<Posting> InvertedIndex::intersect(const std::vector<Posting>& a,
                                              const std::vector<Posting>& b) const {
    std::vector<Posting> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].byte_offset == b[j].byte_offset) {
            result.push_back(Posting(a[i].byte_offset, a[i].score + b[j].score,
                                     std::max(a[i].timestamp, b[j].timestamp)));
            i++;
            j++;
        } else if (a[i].byte_offset < b[j].byte_offset) {
            i++;
        } else {
            j++;
        }
    }
    return result;
}