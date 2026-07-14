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

void InvertedIndex::add_record(const ParsedRecord& record, uint64_t byte_offset,
                               const Tokenizer& tokenizer) {
    // tokenize cmd and cwd
    auto cmd_tokens = tokenizer.tokenize(record.cmd);
    auto cwd_tokens = tokenizer.tokenize(record.cwd);

    // combine cmd and cwd tokens
    auto all_tokens = cmd_tokens;
    all_tokens.insert(all_tokens.end(), cwd_tokens.begin(), cwd_tokens.end());

    // deduplicate per record
    std::unordered_set<std::string> seen;
    for (auto& token : all_tokens) {
        if (seen.find(token) != seen.end()) continue;
        seen.insert(token);
        index_[token].push_back(Posting(byte_offset, 1.0f, record.timestamp));
    }

    total_records_++;
}

std::vector<std::pair<uint64_t, float>> InvertedIndex::search(const std::string& query,
                                                              const Tokenizer& tokenizer,
                                                              size_t top_k) const {
    // tokenize the query
    auto tokens = tokenizer.tokenize(query);
    if (tokens.empty()) return {};

    // get posting lists for each token
    std::vector<std::vector<PostingView>> posting_lists;
    for (auto& token : tokens) {
        auto it = index_.find(token);
        if (it == index_.end()) return {};

        std::vector<PostingView> views;
        views.reserve(it->second.size());
        for (auto& p : it->second) {
            views.push_back({p.byte_offset, p.score, p.timestamp});
        }
        posting_lists.push_back(std::move(views));
    }

    if (posting_lists.empty()) return {};

    // intersect all lists using shared logic
    std::vector<PostingView> result = posting_lists[0];
    for (size_t i = 1; i < posting_lists.size(); i++) {
        result = intersect_views(result, posting_lists[i]);
    }

    if (result.empty()) return {};

    // base score = sum of idf for each query token
    float base_score = 0.0f;
    for (auto& token : tokens) {
        base_score += idf(token);
    }

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    return rank_top_k(result, base_score, now, top_k);
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