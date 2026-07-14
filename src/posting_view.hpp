/**
 * src/posting_view.hpp
 *
 * PostingView — a storage-agnostic representation of a single posting.
 *
 * Both InvertedIndex's in-memory Posting (unordered_map based) and
 * IndexReader's on-disk DiskPosting (mmap based) get converted into
 * this common type before intersection, scoring, and ranking.
 *
 * This lets the intersect + top-K ranking logic be written exactly
 * once and shared between:
 *   - InvertedIndex::search()        (in-memory, daemon + fallback build)
 *   - QueryEngine::search_via_reader() (mmap, fast CLI path)
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

struct PostingView {
    uint64_t byte_offset;
    float score;
    uint64_t timestamp;
};

// intersects two sorted-by-byte_offset PostingView vectors
// summing scores and taking the max timestamp on match
inline std::vector<PostingView> intersect_views(const std::vector<PostingView>& a,
                                                const std::vector<PostingView>& b) {
    std::vector<PostingView> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].byte_offset == b[j].byte_offset) {
            result.push_back({a[i].byte_offset, a[i].score + b[j].score,
                              std::max(a[i].timestamp, b[j].timestamp)});
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

// applies recency boost to base_score for each posting, then returns
// the top_k highest-scoring (byte_offset, final_score) pairs, descending
inline std::vector<std::pair<uint64_t, float>> rank_top_k(const std::vector<PostingView>& postings,
                                                          float base_score, uint64_t now,
                                                          size_t top_k) {
    using ScoreId = std::pair<float, uint64_t>;
    std::priority_queue<ScoreId, std::vector<ScoreId>, std::greater<ScoreId>> pq;

    for (auto& p : postings) {
        float days_ago = static_cast<float>(now - p.timestamp) / 86400.0f;
        float recency_boost = 1.0f + 1.0f / (1.0f + days_ago);
        float final_score = base_score * recency_boost;

        pq.push({final_score, p.byte_offset});
        if (pq.size() > top_k) pq.pop();
    }

    std::vector<std::pair<uint64_t, float>> result;
    while (!pq.empty()) {
        result.push_back({pq.top().second, pq.top().first});
        pq.pop();
    }
    std::reverse(result.begin(), result.end());
    return result;
}