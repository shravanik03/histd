/**
 * src/renderer.hpp
 *
 * Renderer — terminal output for histd search results and failure groups.
 *
 * Header-only — all functions are inline since they are simple enough
 * to not warrant a separate .cpp file.
 *
 * Uses ANSI escape codes for colored terminal output.
 * All functions write to stdout.
 */

#pragma once

#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "failure_clusterer.hpp"
#include "query_engine.hpp"

// ANSI color constants
inline constexpr const char* RESET = "\033[0m";
inline constexpr const char* BOLD = "\033[1m";
inline constexpr const char* GREEN = "\033[32m";
inline constexpr const char* RED = "\033[31m";
inline constexpr const char* YELLOW = "\033[33m";
inline constexpr const char* BLUE = "\033[34m";
inline constexpr const char* DIM = "\033[90m";

// converts epoch timestamp to "N days ago" style string
// < 60 seconds  → "just now"
// < 60 minutes  → "N minutes ago"
// < 24 hours    → "N hours ago"
// < 7 days      → "N days ago"
// < 30 days     → "N weeks ago"
// otherwise     → "N months ago"
inline std::string relative_time(uint64_t timestamp) {
    auto now = static_cast<uint64_t>(std::time(nullptr));
    auto diff = now - timestamp;

    if (diff < 60) return "just now";
    if (diff < 60 * 60) return std::to_string(diff / 60) + " minutes ago";
    if (diff < 24 * 60 * 60) return std::to_string(diff / (60 * 60)) + " hours ago";
    if (diff < 7 * 24 * 60 * 60) return std::to_string(diff / (24 * 60 * 60)) + " days ago";
    if (diff < 30 * 24 * 60 * 60) return std::to_string(diff / (7 * 24 * 60 * 60)) + " weeks ago";
    return std::to_string(diff / (30 * 24 * 60 * 60)) + " months ago";
}

// converts milliseconds to human readable duration string
// < 1000ms  → "Xms"
// < 60000ms → "X.Ys"
// otherwise → "Xm Ys"
inline std::string format_duration(uint32_t duration_ms) {
    if (duration_ms < 1000) return std::to_string(duration_ms) + "ms";
    if (duration_ms < 60000)
        return std::to_string(duration_ms / 1000) + "." +
               std::to_string((duration_ms % 1000) / 100) + "s";
    uint32_t minutes = duration_ms / 60000;
    uint32_t seconds = (duration_ms % 60000) / 1000;
    return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
}

// renders search results to stdout with colored output
// shows result number, exit status (✓/✗), command, score, and metadata
inline void render_search_results(const std::vector<SearchResult>& results,
                                  const std::string& query, size_t total_records) {
    auto found = results.size();
    if (found == 0) {
        std::cout << DIM << "No results found for \"" << query << "\"" << RESET << "\n";
        return;
    }
    std::cout << BOLD << "Found " << found << " result" << (found == 1 ? "" : "s") << " for \""
              << query << "\"  " << RESET;
    std::cout << DIM << "(" << total_records << " records indexed)" << RESET << "\n\n";

    int cnt = 0;
    for (const auto& result : results) {
        std::cout << BLUE << ++cnt << RESET;
        if (result.record.exit_code == 0) {
            std::cout << GREEN << "  ✓ " << RESET;
        } else {
            std::cout << RED << "  ✗ " << RESET;
        }
        std::cout << result.record.cmd;
        std::cout << YELLOW << "  score " << result.score << RESET << "\n";
        if (result.record.exit_code == 0)
            std::cout << DIM << result.record.cwd << " . " << relative_time(result.record.timestamp)
                      << " . " << format_duration(result.record.duration_ms) << RESET << "\n";
        else
            std::cout << DIM << result.record.cwd << " . " << relative_time(result.record.timestamp)
                      << " . " << format_duration(result.record.duration_ms) << " . "
                      << result.record.exit_code << RESET << "\n";
    }
}

// renders top failing command groups to stdout
// shows failure count, normalized command, last seen time, and directories
inline void render_failures(const std::vector<FailureGroup>& groups) {
    std::cout << BOLD << "Top failing commands" << RESET << "\n\n";
    for (auto& group : groups) {
        std::cout << RED << "  × " << group.count << " times  " << RESET << group.normalized_cmd
                  << "\n";
        std::cout << DIM << "  last seen: " << relative_time(group.last_seen) << "  ·  exit "
                  << group.exit_code << RESET << "\n";
        std::cout << DIM << "  dirs: ";
        for (auto& dir : group.dirs) std::cout << dir << "  ";
        std::cout << RESET << "\n\n";
    }
}

// renders index statistics to stdout
inline void render_status(size_t total_records, size_t term_count) {
    std::cout << "Records indexed:   " << total_records << "\n";
    std::cout << "Unique terms:      " << term_count << "\n";
    std::cout << "Index built:       lazily on first search\n";
}