/**
 * src/index_store.hpp
 *
 * Persistent binary format for InvertedIndex — index.bin
 *
 * Enables the daemon to persist its in-memory index to disk, and the
 * hist CLI to search it via memory-mapped binary search instead of
 * deserializing into an unordered_map.
 *
 * File layout:
 *
 *   [IndexHeader]           37 bytes, once per file
 *   [TermEntry × N]         22 bytes each, sorted by term_hash,
 *                           one entry per unique term, enables O(log N)
 *                           binary search lookup
 *   [term strings]          packed, variable length, referenced by
 *                           TermEntry::term_str_off
 *   [DiskPosting × M]       20 bytes each, packed contiguously,
 *                           referenced by TermEntry::post_off/post_count
 *
 * IndexSerializer converts an in-memory InvertedIndex (unordered_map)
 * into this on-disk format. Writes atomically via temp file + rename.
 *
 * IndexReader mmaps the file and provides find_postings() — binary
 * search on the term directory by FNV-1a hash, with a string comparison
 * to guard against hash collisions. Returns a pointer directly into the
 * mmap — zero deserialization, zero copies.
 *
 * All on-disk structs use #pragma pack(1) to guarantee exact byte
 * layout with no compiler-inserted padding.
 */
#pragma once

#include <cstdint>
#include <string>

#include "inverted_index.hpp"

// The on-disk structs - these descibe the binary layout of the index file

#pragma pack(push, 1)
// the file header appears once at the start of index.bin
struct IndexHeader {
    char magic[4];            // "HIDX"
    uint8_t version;          // 1
    uint64_t last_offset;     // byte position in records.bin indexed so far
    uint64_t total_records;   // number of records indexed
    uint64_t term_count;      // number of unique terms
    uint32_t strings_start;   // byte offset where term strings section begins
    uint32_t postings_start;  // byte offset where postings section begins
};

// one entry per unique term, fixed size for binary search
struct TermEntry {
    uint32_t term_hash;     // FNV-1a hash of the term
    uint32_t term_str_off;  // offset into strings section
    uint16_t term_len;      // length of term string
    uint64_t post_off;      // offset into postings section
    uint32_t post_count;    // number of postings for this term
};

// one posting — 20 bytes, matches the in-memory Posting
struct DiskPosting {
    uint64_t byte_offset;  // position in records.bin
    float score;           // base score
    uint64_t timestamp;    // for recency boost
};
#pragma pack(pop)

class IndexSerializer {
   public:
    // writes the in-memory index to path in binary format
    // last_offset = byte position in records.bin indexed so far
    // uses atomic write (temp file + rename)
    static void save(const InvertedIndex& index, const std::string& path, uint64_t last_offset);

   private:
    // struct for storing the terms while serializing
    struct PendingEntry {
        std::string term;
        uint32_t term_hash;
        uint32_t term_str_off;
        uint16_t term_len;
        uint64_t post_off;
        uint32_t post_count;
        const std::vector<Posting>* postings;
    };
};

class IndexReader {
   public:
    // mmaps index.bin
    explicit IndexReader(const std::string& path);
    ~IndexReader();

    // true if the file was successfully opened and mapped
    bool valid() const;

    // binary search for a term
    // if found: sets *out to point at the posting array in the mmap
    //           sets *count to number of postings
    //           returns true
    // if not found: returns false
    bool find_postings(const std::string& term, const DiskPosting** out, uint32_t* count) const;

    uint64_t last_offset() const;
    uint64_t total_records() const;
    size_t term_count() const;

   private:
    int fd_;
    void* mapped_;
    size_t file_size_;

    const IndexHeader* header_;    // points into mmap at offset 0
    const TermEntry* directory_;   // points into mmap at term directory
    const char* strings_;          // points into mmap at strings section
    const DiskPosting* postings_;  // points into mmap at postings section

    // binary search the directory by hash
    // returns pointer to TermEntry or nullptr
    const TermEntry* find_entry(uint32_t hash, const std::string& term) const;
};