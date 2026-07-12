/**
 * tests/test_failure_clusterer.cpp
 *
 * Manual test for FailureClusterer.
 * Inserts known failed records and verifies clustering output.
 */
#include <iostream>

#include "failure_clusterer.hpp"
#include "record_store.hpp"

int main() {
    std::string records_path = std::string(getenv("HOME")) + "/.local/share/histd/records.bin";

    RecordStore store(records_path, Mode::READ);
    std::cout << "Records in store: " << store.count() << "\n\n";

    FailureClusterer clusterer;
    auto groups = clusterer.cluster(store, 10);

    if (groups.empty()) {
        std::cout << "No failure groups found.\n";
        std::cout << "This is expected if all records have exit_code == 0.\n";
        return 0;
    }

    std::cout << "Top failing commands:\n\n";
    for (auto& group : groups) {
        std::cout << "  x " << group.count << " times   " << group.normalized_cmd << "\n";
        std::cout << "    exit code: " << group.exit_code << "\n";
        std::cout << "    dirs: ";
        for (auto& dir : group.dirs) std::cout << dir << "  ";
        std::cout << "\n\n";
    }

    return 0;
}