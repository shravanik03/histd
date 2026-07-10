/**
 * src/record_store.hpp
 *
 * RecordStore — persistent binary storage for command records.
 *
 * Wraps records.bin with two operating modes:
 *
 *   WRITE mode (daemon):
 *     Opens records.bin for appending. Serializes ParsedRecord structs
 *     into the binary CommandRecord format and writes them to disk using
 *     pwrite(). Tracks file size as a member variable to know where each
 *     new record should be written. No mmap involved.
 *
 *   READ mode (hist CLI):
 *     Opens records.bin and mmaps the entire file for zero-copy reading.
 *     Iterates records by walking the mmap pointer, reading each fixed
 *     24-byte header and jumping by cmd_len + cwd_len to the next record.
 *     Deserializes binary data back into ParsedRecord structs.
 *
 * Only one process writes (daemon). Multiple processes can read
 * simultaneously (hist CLI) since they only mmap with PROT_READ.
 */
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <functional>
#include <string>

#include "record.hpp"

enum class Mode { READ, WRITE };

class RecordStore {
   public:
    // constructor — opens file in given mode
    // WRITE: creates file if not exists, tracks file size for appending
    // READ:  mmaps the file for zero-copy reading
    explicit RecordStore(const std::string& path, Mode mode);
    // destructor — closes fd, unmaps if read mode
    ~RecordStore();

    // WRITE mode only
    // serializes ParsedRecord to binary and appends to disk
    // returns true on success, false on failure or wrong mode
    bool append(const ParsedRecord& record);

    // READ mode only
    // calls callback once for every record in the file
    // callback receives a fully populated ParsedRecord
    void for_each(std::function<void(const ParsedRecord&)> callback) const;

    // Returns the number of records in the file
    size_t count() const;

   private:
    std::string path_;     // path to records.bin
    Mode mode_;            // READ or WRITE
    int fd_;               // file descriptor from open
    void* mapped_;         // mmap pointer (READ mode only, nullptr in WRITE mode)
    size_t file_size_;     // current file size in bytes
    size_t record_count_;  // number of records (computed on open)

    // scans the file once at startup to count existing records
    // needed when daemon restarts and records.bin already has data
    void count_records();

    // deserializes one record from mmap at the given pointer position
    // ptr must point to the start of a CommandRecord header
    ParsedRecord deserialize(const char* ptr) const;

    // serializes a ParsedRecord into a CommandRecord binary header
    // used by append() before writing to disk
    CommandRecord serialize(const ParsedRecord& record) const;
};