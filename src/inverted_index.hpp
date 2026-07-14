/**
 * src/inverted_index.hpp
 *
 * InvertedIndex — maps tokens to the records that contain them,
 * enabling fast full-text search over command history.
 *
 * Structure:
 *   token → sorted vector of Posting{byte_offset, score}
 *
 * Two phases:
 *   build()   — scans all records in RecordStore, tokenizes cmd and cwd,
 *               populates the index. O(N × tokens_per_record).
 *   search()  — tokenizes query, looks up each token's posting list,
 *               intersects all lists, ranks by TF-IDF + recency boost,
 *               returns top-K record IDs. O(matches).
 *
 * Posting lists are sorted by byte_offset to enable efficient two-pointer
 * intersection. IDF is computed at search time using total_records_.
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "record_store.hpp"
#include "tokenizer.hpp"

struct Posting {
    uint64_t byte_offset;
    float score;
    uint64_t timestamp;

    // constructor for easy initialization
    Posting(uint64_t offset, float s, uint64_t ts) : byte_offset(offset), score(s), timestamp(ts) {}

    // sort by byte_offset for two-pointer intersection
    bool operator<(const Posting& other) const { return byte_offset < other.byte_offset; }
};

class InvertedIndex {
    // IndexSerializer needs direct access to index_ and total_records_
    // to serialize the in-memory index to index.bin
    friend class IndexSerializer;

   public:
    // builds index by scanning all records in store
    // tokenizes cmd and cwd fields of each record
    void build(RecordStore& store, const Tokenizer& tokenizer);

    // adds a single record to the index incrementally
    // used by the daemon as new commands arrive, avoiding a full rebuild
    // does not re-sort posting lists — relies on byte_offset being
    // monotonically increasing since records are appended in order
    void add_record(const ParsedRecord& record, uint64_t byte_offset, const Tokenizer& tokenizer);

    // searches for top_k records matching all query tokens
    // returns record IDs sorted by score descending
    std::vector<std::pair<uint64_t, float>> search(const std::string& query,
                                                   const Tokenizer& tokenizer,
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