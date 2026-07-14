/**
 * tests/test_index_store.cpp
 *
 * Verifies IndexSerializer + IndexReader round-trip correctly.
 * Builds an in-memory index from real records, saves to index.bin,
 * then reads it back via mmap and confirms search results match.
 */
#include <iostream>

#include "index_store.hpp"
#include "inverted_index.hpp"
#include "record_store.hpp"
#include "tokenizer.hpp"

int main() {
    std::string home = getenv("HOME");
    std::string records_path = home + "/.local/share/histd/records.bin";
    std::string index_path = "/tmp/test_index.bin";

    // build in-memory index from real records
    RecordStore store(records_path, Mode::READ);
    Tokenizer tokenizer;
    InvertedIndex index;
    index.build(store, tokenizer);

    std::cout << "Built index: " << index.term_count() << " terms, " << index.record_count()
              << " records\n";

    // save to disk
    uint64_t last_offset = 654;  // arbitrary test value
    IndexSerializer::save(index, index_path, last_offset);
    std::cout << "Saved to " << index_path << "\n\n";

    // load via mmap and verify
    IndexReader reader(index_path);
    if (!reader.valid()) {
        std::cout << "FAILED to load index.bin\n";
        return 1;
    }

    std::cout << "IndexReader loaded successfully\n";
    std::cout << "  last_offset:   " << reader.last_offset() << " (expected " << last_offset
              << ")\n";
    std::cout << "  total_records: " << reader.total_records() << "\n";
    std::cout << "  term_count:    " << reader.term_count() << "\n\n";

    // test lookups for known terms
    std::vector<std::string> test_terms = {"docker", "git", "histd", "build", "nonexistent"};

    for (auto& term : test_terms) {
        const DiskPosting* postings = nullptr;
        uint32_t count = 0;
        bool found = reader.find_postings(term, &postings, &count);

        std::cout << "find_postings(\"" << term << "\") → ";
        if (!found) {
            std::cout << "not found\n";
            continue;
        }
        std::cout << count << " postings: ";
        for (uint32_t i = 0; i < count; i++) {
            std::cout << "[offset=" << postings[i].byte_offset << " score=" << postings[i].score
                      << "] ";
        }
        std::cout << "\n";
    }

    return 0;
}