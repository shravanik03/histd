/**
 * cli/main.cpp
 *
 * hist — CLI entry point for the histd search engine.
 *
 * Subcommands:
 *   search <query>              search command history by keyword
 *   search <query> --failed     only show failed commands
 *   search <query> --since Nd   commands from last N days
 *   search <query> --project P  filter by directory prefix
 *   failures                    show top repeatedly failing commands
 *   status                      show index and record statistics
 *
 * Reads from ~/.local/share/histd/records.bin
 * Index is built lazily on first search call.
 */
#include <cstdint>
#include <iostream>
#include <string>

#include "failure_clusterer.hpp"
#include "query_engine.hpp"
#include "record_store.hpp"
#include "renderer.hpp"

void print_usage() {
    std::cout << "histd — Smart Terminal History Engine\n\n";
    std::cout << "Usage:\n";
    std::cout << "  hist search <query>           search command history\n";
    std::cout << "  hist search <query> --failed  only show failed commands\n";
    std::cout << "  hist search <query> --since Nd  commands from last N days\n";
    std::cout << "  hist search <query> --project <path>  filter by directory\n";
    std::cout << "  hist failures                 show top failing commands\n";
    std::cout << "  hist status                   show index statistics\n";
}

int main(int argc, char* argv[]) {
    const char* home = std::getenv("HOME");

    if (!home) {
        std::cerr << "HOME environment variable not set" << std::endl;
        exit(1);
    }

    std::string HOME_PATH = home;
    std::string DATA_DIR = HOME_PATH + "/.local/share/histd";
    std::string records_path = DATA_DIR + "/records.bin";

    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string subcmd = std::string(argv[1]);

    if (subcmd == "search") {
        std::string query;
        bool failed_only = false;
        uint32_t since_days = 0;
        std::string project;

        for (int i = 2; i < argc; i++) {
            std::string arg = std::string(argv[i]);
            if (arg == "--failed") {
                failed_only = true;
            } else if (arg == "--since" && i + 1 < argc) {
                // next argument is "7d" — extract the number
                since_days = static_cast<uint32_t>(std::stoul(argv[i + 1]));
                i++;  // skip the next argument since we consumed it
            } else if (arg == "--project" && i + 1 < argc) {
                project = std::string(argv[i + 1]);
                i++;  // skip the next argument since we consumed it
            } else if (arg[0] != '-') {
                // not a flag — it is the query string
                query = arg;
            }
        }

        if (query.empty()) {
            print_usage();
            return 1;
        }

        QueryEngine engine(records_path);
        auto results = engine.search(query, failed_only, since_days, project);
        render_search_results(results, query, engine.record_count());

    } else if (subcmd == "failures") {
        RecordStore store(records_path, Mode::READ);
        FailureClusterer clusterer;
        auto failed_groups = clusterer.cluster(store);
        render_failures(failed_groups);
    } else if (subcmd == "status") {
        QueryEngine engine(records_path);
        render_status(engine.record_count(), engine.term_count());
    } else {
        print_usage();
        return 1;
    }

    return 0;
}