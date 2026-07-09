/**
 * src/record.hpp
 *
 * Core data structures for histd.
 *
 * Defines two representations of a command record:
 *
 *   CommandRecord  — the binary layout written to disk (records.bin).
 *                    Fixed 24-byte header followed by variable-length
 *                    cmd and cwd strings packed contiguously. No padding,
 *                    no separators. The header fields cmd_len and cwd_len
 *                    tell the reader exactly how many bytes follow.
 *
 *   ParsedRecord   — the in-memory representation used by the rest of
 *                    the codebase. Holds the same data but uses std::string
 *                    for cmd and cwd instead of raw byte lengths.
 *                    Daemon parses incoming socket data into ParsedRecord.
 *                    RecordStore serializes ParsedRecord → CommandRecord
 *                    when writing and deserializes CommandRecord → ParsedRecord
 *                    when reading.
 *
 * Also defines fnv1a_hash() — a fast, stable 32-bit hash used to store
 * the session ID string as a fixed 4-byte value in the binary header.
 * FNV-1a chosen over std::hash because std::hash is not stable across
 * process restarts, which would break persistent file lookups.
 *
 * Disk layout of one record:
 *
 *   [timestamp:    8 bytes]
 *   [duration_ms:  4 bytes]
 *   [exit_code:    4 bytes]
 *   [session_hash: 4 bytes]
 *   [cmd_len:      2 bytes]
 *   [cwd_len:      2 bytes]
 *   [cmd:          cmd_len bytes]
 *   [cwd:          cwd_len bytes]
 *   total fixed header = 24 bytes
 */
#pragma once

#include <cstdint>
#include <string>

struct CommandRecord {
    uint64_t timestamp;     // Unix epoch seconds — 8 bytes, avoids year 2038 problem of uint32
    uint32_t duration_ms;   // command duration in ms — 4 bytes, max ~49 days, sufficient
    int32_t exit_code;      // signed — exit codes can be negative on some systems
    uint32_t session_hash;  // FNV-1a hash of session string — 4 bytes fixed from variable string
    uint16_t cmd_len;       // length of cmd string in bytes — 2 bytes, max 65535 chars
    uint16_t cwd_len;       // length of cwd string in bytes — 2 bytes, max 65535 chars
};  // sizeof(CommandRecord) == 24 bytes — fixed header size on disk

struct ParsedRecord {
    uint64_t timestamp;
    uint32_t duration_ms;
    int32_t exit_code;
    std::string session_id;
    std::string cmd;
    std::string cwd;
};

/**
 * Inline hash function to convert the session string to a 4-byte number
 */
inline uint32_t fnv1a_hash(const std::string &session_id) {
    uint32_t hash = 2166136261u;
    for (char c : session_id) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}
