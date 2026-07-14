/**
 * src/index_store.cpp
 *
 * Implementation of IndexSerializer and IndexReader.
 * See index_store.hpp for binary format documentation.
 */

#include "index_store.hpp"

#include <fcntl.h>     // open, O_RDONLY
#include <sys/mman.h>  // mmap, munmap, PROT_READ, MAP_PRIVATE
#include <unistd.h>    // lseek, close

#include <algorithm>  // std::sort
#include <cstdio>     // std::rename
#include <cstring>    // std::memcpy
#include <fstream>    // std::ofstream, std::ifstream
#include <iostream>

void IndexSerializer::save(const InvertedIndex& index, const std::string& path,
                           uint64_t last_offset) {
    uint64_t term_count = index.term_count();
    uint64_t total_records = index.record_count();
    uint64_t term_directory_size = term_count * sizeof(TermEntry);
    uint32_t strings_start = sizeof(IndexHeader) + term_directory_size;

    // Pass 1 — build pending entries WITHOUT offsets yet
    std::vector<PendingEntry> pending_entries_;
    for (const auto& entry : index.index_) {
        PendingEntry pending_entry;
        pending_entry.term = entry.first;
        pending_entry.term_hash = fnv1a_hash(entry.first);
        pending_entry.term_len = entry.first.length();
        pending_entry.post_count = entry.second.size();
        pending_entry.postings = &entry.second;
        pending_entries_.push_back(pending_entry);
    }

    std::sort(
        pending_entries_.begin(), pending_entries_.end(),
        [](const PendingEntry& a, const PendingEntry& b) { return a.term_hash < b.term_hash; });

    // Pass 2 — NOW compute offsets, in the same order they will be written
    uint64_t current_string_offset = 0;
    uint64_t current_posting_offset = 0;

    for (auto& entry : pending_entries_) {
        entry.term_str_off = current_string_offset;
        entry.post_off = current_posting_offset;

        current_string_offset += entry.term.length();
        current_posting_offset += entry.post_count * sizeof(DiskPosting);
    }

    uint64_t strings_size = current_string_offset;
    uint64_t postings_start = strings_start + strings_size;

    std::string tmp_path = path + ".tmp";
    std::ofstream file(tmp_path, std::ios::binary);

    // write the file header
    IndexHeader header;

    memcpy(header.magic, "HIDX", 4);
    header.version = 1;
    header.last_offset = last_offset;
    header.total_records = total_records;
    header.term_count = term_count;
    header.strings_start = strings_start;
    header.postings_start = postings_start;

    file.write(reinterpret_cast<const char*>(&header), sizeof(IndexHeader));

    // write the Term Directory
    for (const auto& entry : pending_entries_) {
        TermEntry term_entry;

        term_entry.term_hash = entry.term_hash;
        term_entry.term_str_off = entry.term_str_off;
        term_entry.term_len = entry.term_len;
        term_entry.post_off = entry.post_off;
        term_entry.post_count = entry.post_count;

        file.write(reinterpret_cast<const char*>(&term_entry), sizeof(TermEntry));
    }

    // write the Term Strings
    for (const auto& entry : pending_entries_) {
        file.write(entry.term.data(), entry.term.size());
    }

    // write the Postings
    for (const auto& entry : pending_entries_) {
        for (const auto& posting : *entry.postings) {
            DiskPosting disk_posting;
            disk_posting.byte_offset = posting.byte_offset;
            disk_posting.score = posting.score;
            disk_posting.timestamp = posting.timestamp;
            file.write(reinterpret_cast<const char*>(&disk_posting), sizeof(DiskPosting));
        }
    }

    file.close();
    std::rename(tmp_path.c_str(), path.c_str());
}

IndexReader::IndexReader(const std::string& path) {
    fd_ = open(path.c_str(), O_RDONLY);
    if (fd_ < 0) {
        mapped_ = nullptr;
        return;
    }
    file_size_ = static_cast<size_t>(lseek(fd_, 0, SEEK_END));
    if (file_size_ == 0) {
        mapped_ = nullptr;
        close(fd_);
        return;
    }
    mapped_ = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (mapped_ == MAP_FAILED) {
        mapped_ = nullptr;
        close(fd_);
        return;
    }

    const char* base = static_cast<const char*>(mapped_);
    header_ = reinterpret_cast<const IndexHeader*>(base);

    // verify magic bytes
    if (memcmp(header_->magic, "HIDX", 4) != 0) {
        munmap(mapped_, file_size_);
        mapped_ = nullptr;
        close(fd_);
        return;
    }

    directory_ = reinterpret_cast<const TermEntry*>(base + sizeof(IndexHeader));
    strings_ = reinterpret_cast<const char*>(base + header_->strings_start);
    postings_ = reinterpret_cast<const DiskPosting*>(base + header_->postings_start);
}

IndexReader::~IndexReader() {
    if (mapped_ != nullptr) {
        munmap(mapped_, file_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool IndexReader::valid() const { return mapped_ != nullptr; }

bool IndexReader::find_postings(const std::string& term, const DiskPosting** out,
                                uint32_t* count) const {
    if (!valid()) return false;

    uint32_t hash = fnv1a_hash(term);
    const TermEntry* entry = find_entry(hash, term);

    if (entry == nullptr) return false;

    *out = postings_ + (entry->post_off / sizeof(DiskPosting));
    *count = entry->post_count;
    return true;
}

uint64_t IndexReader::last_offset() const { return header_->last_offset; }

uint64_t IndexReader::total_records() const { return header_->total_records; }

size_t IndexReader::term_count() const { return static_cast<size_t>(header_->term_count); }

const TermEntry* IndexReader::find_entry(uint32_t hash, const std::string& term) const {
    size_t low = 0;
    size_t high = header_->term_count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const TermEntry& entry = directory_[mid];
        if (entry.term_hash == hash) {
            // we found the record, verify actual string to guard against collision
            const char* str_ptr = strings_ + entry.term_str_off;
            if (entry.term_len == term.size() &&
                std::memcmp(str_ptr, term.data(), term.size()) == 0) {
                return &entry;
            }
            return nullptr;
        } else if (entry.term_hash < hash) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return nullptr;
}