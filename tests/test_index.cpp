/**
 * tests/test_index.cpp
 *
 * End-to-end test for InvertedIndex.
 * Builds index from real records.bin and searches for tokens.
 */
#include <iostream>

#include "inverted_index.hpp"
#include "record_store.hpp"
#include "tokenizer.hpp"

int main() {
    std::string data_dir = std::string(getenv("HOME")) + "/.local/share/histd";
    std::string records_path = data_dir + "/records.bin";

    // open record store in READ mode
    RecordStore store(records_path, Mode::READ);
    std::cout << "Records in store: " << store.count() << "\n";

    if (store.count() == 0) {
        std::cout << "No records found. Run some commands first.\n";
        return 1;
    }

    // print all records so we know what to search for
    std::cout << "\nAll records:\n";
    uint32_t id = 0;
    store.for_each([&](const ParsedRecord& rec) {
        std::cout << "[" << id++ << "] " << rec.cmd << " | " << rec.cwd << "\n";
    });

    // build index
    Tokenizer tokenizer;
    InvertedIndex index;
    index.build(store, tokenizer);

    std::cout << "\nIndex built.\n";
    std::cout << "Terms indexed: " << index.term_count() << "\n";
    std::cout << "Records indexed: " << index.record_count() << "\n";

    // search for something you know exists in your records
    std::vector<std::string> queries = {"histd", "build", "git", "docker"};

    for (auto& query : queries) {
        auto results = index.search(query, tokenizer);
        std::cout << "\nsearch(\"" << query << "\") → " << results.size() << " results: ";
        for (auto id : results) std::cout << "[" << id << "] ";
        std::cout << "\n";
    }

    return 0;
}