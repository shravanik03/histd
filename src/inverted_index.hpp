/**
 * src/inverted_index.hpp
 *
 * InvertedIndex — maps tokens to the records that contain them,
 * enabling fast full-text search over command history.
 *
 * Structure:
 *   token → sorted vector of Posting{record_id, score}
 *
 * Two phases:
 *   build()   — scans all records in RecordStore, tokenizes cmd and cwd,
 *               populates the index. O(N × tokens_per_record).
 *   search()  — tokenizes query, looks up each token's posting list,
 *               intersects all lists, ranks by TF-IDF + recency boost,
 *               returns top-K record IDs. O(matches).
 *
 * Posting lists are sorted by record_id to enable efficient two-pointer
 * intersection. IDF is computed at search time using total_records_.
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "record_store.hpp"
#include "tokenizer.hpp"

struct Posting {
    uint32_t record_id;
    float score;
    uint64_t timestamp;

    // constructor for easy initialization
    Posting(uint32_t id, float s, uint64_t ts) : record_id(id), score(s), timestamp(ts) {}

    // sort by record_id for two-pointer intersection
    bool operator<(const Posting& other) const { return record_id < other.record_id; }
};

class InvertedIndex {
   public:
    // builds index by scanning all records in store
    // tokenizes cmd and cwd fields of each record
    void build(RecordStore& store, const Tokenizer& tokenizer);

    // searches for top_k records matching all query tokens
    // returns record IDs sorted by score descending
    std::vector<uint32_t> search(const std::string& query, const Tokenizer& tokenizer,
                                 size_t top_k = 10) const;

    // number of unique tokens in the index
    size_t term_count() const;

    // number of records indexed
    size_t record_count() const;

   private:
    // token → sorted posting list
    std::unordered_map<std::string, std::vector<Posting>> index_;

    // total records indexed — needed for IDF
    size_t total_records_ = 0;

    // computes IDF score for a token
    // IDF = log(total_records / records_containing_token)
    float idf(const std::string& token) const;

    // intersects two sorted posting lists using two-pointer technique
    // returns postings present in both with summed scores
    std::vector<Posting> intersect(const std::vector<Posting>& a,
                                   const std::vector<Posting>& b) const;
};