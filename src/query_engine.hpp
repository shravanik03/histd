/**
 * src/query_engine.hpp
 *
 * QueryEngine — the single entry point for searching command history.
 *
 * Owns and coordinates RecordStore, InvertedIndex, and Tokenizer.
 * The hist CLI only interacts with QueryEngine — it never touches
 * the index or store directly.
 *
 * Search pipeline:
 *   1. ensure_index() — builds InvertedIndex from RecordStore on first call
 *   2. index_.search() — returns byte offsets of matching records
 *   3. apply_filters() — reads each record and applies:
 *        --failed     exit_code != 0 only
 *        --since Nd   commands from last N days only
 *        --project    commands run from a specific directory only
 *   4. returns vector<SearchResult> ranked by TF-IDF + recency score
 *
 * Index is built lazily — only on the first search() call.
 * Subsequent searches reuse the in-memory index.
 */
#pragma once

#include <cstdint>
#include <memory>  // std::unique_ptr
#include <string>
#include <vector>

#include "index_store.hpp"
#include "inverted_index.hpp"
#include "posting_view.hpp"
#include "record_store.hpp"
#include "tokenizer.hpp"

struct SearchResult {
    ParsedRecord record;   // full command data
    float score;           // final ranking score
    uint64_t byte_offset;  // byte offset in records.bin

    SearchResult(ParsedRecord r, float s, uint64_t o) : record(r), score(s), byte_offset(o) {}
};

class QueryEngine {
   public:
    // constructor takes paths to data files
    QueryEngine(const std::string& records_path);

    // main search function
    // query: the search string
    // failed_only: only return failed commands
    // since_days: only return commands from last N days (0 = no filter)
    // project: only return commands from this directory (empty = no filter)
    // top_k: max results to return
    std::vector<SearchResult> search(const std::string& query, bool failed_only = false,
                                     uint32_t since_days = 0, const std::string& project = "",
                                     size_t top_k = 10);

    size_t record_count() const;

    size_t term_count();

   private:
    RecordStore store_;
    InvertedIndex index_;
    Tokenizer tokenizer_;
    bool index_built_ = false;

    std::unique_ptr<IndexReader> reader_;  // nullptr if not using mmap path

    // ensures index is built before searching
    void ensure_index();

    // applies post-search filters to results
    std::vector<SearchResult> apply_filters(const std::vector<std::pair<uint64_t, float>>& results,
                                            bool failed_only, uint32_t since_days,
                                            const std::string& project) const;

    // search using the IndexReader
    std::vector<std::pair<uint64_t, float>> search_via_reader(const std::string& query,
                                                              size_t top_k) const;
};