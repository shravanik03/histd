/**
 * src/record_store.cpp
 *
 * Implementation of RecordStore — persistent binary storage for
 * command records captured by the histd shell hook.
 *
 * See record_store.hpp for full design notes and mode documentation.
 */
#include "record_store.hpp"

#include <cstring>   // for memset
#include <iostream>  // for error messages

/**
 * Constructor
 * Opens records.bin in the given mode.
 * WRITE: creates file if not exists, seeks to end for appending
 * READ:  opens read-only and mmaps the entire file
 * In both modes, counts existing records by scanning the file.
 */
RecordStore::RecordStore(const std::string& path, Mode mode) : path_(path), mode_(mode) {
    mapped_ = nullptr;
    record_count_ = 0;

    if (mode_ == Mode::WRITE) {
        // open file for appending
        fd_ = open(path_.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd_ < 0) {
            std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
            exit(1);
        }
        file_size_ = lseek(fd_, 0, SEEK_END);
        count_records();
    } else {
        // mmap file for zero-copy reading
        fd_ = open(path_.c_str(), O_RDONLY);
        if (fd_ < 0) {
            std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
            exit(1);
        }
        file_size_ = lseek(fd_, 0, SEEK_END);
        if (file_size_ > 0) {
            mapped_ = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (mapped_ == MAP_FAILED) {
                std::cerr << "Failed to mmap file: " << strerror(errno) << "\n";
                mapped_ = nullptr;
                exit(1);
            }
        }
    }
}

/**
 * Destructor
 * Unmaps memory if in READ mode, closes file descriptor.
 */
RecordStore::~RecordStore() {
    if (mode_ == Mode::WRITE) {
        close(fd_);
    } else {
        if (mapped_ != nullptr) {
            munmap(mapped_, file_size_);
        }
        close(fd_);
    }
}

/**
 * Appends one command record to records.bin.
 * Serializes the ParsedRecord into a binary CommandRecord header
 * followed by the raw cmd and cwd string bytes.
 * Updates file_size_ and record_count_ after writing.
 * Returns false immediately if called in READ mode.
 */
bool RecordStore::append(const ParsedRecord& record) {
    if (mode_ == Mode::READ) return false;

    // build CommandRecord header - serialize ParsedRecord
    CommandRecord header = serialize(record);

    // write header at current file_size_ offset
    pwrite(fd_, &header, sizeof(header), static_cast<off_t>(file_size_));

    // write cmd string bytes at file_size_ + sizeof(header) offset
    pwrite(fd_, record.cmd.c_str(), record.cmd.size(),
           static_cast<off_t>(file_size_ + sizeof(header)));

    // write cwd string bytes after cmd
    pwrite(fd_, record.cwd.c_str(), record.cwd.size(),
           static_cast<off_t>(file_size_ + sizeof(header) + record.cmd.size()));

    file_size_ += sizeof(header) + record.cmd.size() + record.cwd.size();
    record_count_++;
    return true;
}

/**
 * Iterates every record in records.bin and calls callback on each.
 * Walks the mmap pointer from start to end, deserializing each record.
 * No-op if called in WRITE mode or if the file is empty.
 * TODO: implement in Week 5 when building the hist CLI.
 */
void RecordStore::for_each(std::function<void(const ParsedRecord&)> callback) const {
    // TODO: implement when building hist CLI (Week 5)
    (void)callback;  // suppress unused parameter warning
}

/**
 * Serializes a ParsedRecord into a CommandRecord binary header.
 * Hashes the session_id string to a fixed 4-byte value.
 * Sets cmd_len and cwd_len to the byte lengths of the strings.
 * The strings themselves are not included — written separately by append().
 */
CommandRecord RecordStore::serialize(const ParsedRecord& record) const {
    CommandRecord header;
    memset(&header, 0, sizeof(CommandRecord));
    header.timestamp = record.timestamp;
    header.duration_ms = record.duration_ms;
    header.exit_code = record.exit_code;
    header.session_hash = fnv1a_hash(record.session_id);
    header.cmd_len = static_cast<uint16_t>(record.cmd.size());
    header.cwd_len = static_cast<uint16_t>(record.cwd.size());
    return header;
}

/**
 * Scans records.bin from the beginning counting every record.
 * Called once in the constructor to initialize record_count_
 * when the file already contains records from a previous session.
 * Navigates by reading each 24-byte header and jumping by
 * sizeof(header) + cmd_len + cwd_len to reach the next record.
 */
void RecordStore::count_records() {
    // only called in WRITE mode — READ mode uses mmap to count
    if (mode_ == Mode::READ) return;
    if (file_size_ == 0) return;

    record_count_ = 0;
    off_t offset = 0;

    while (offset < static_cast<off_t>(file_size_)) {
        CommandRecord header;

        // if we cannot read a full header stop — file may be truncated
        ssize_t bytes_read = pread(fd_, &header, sizeof(header), offset);
        if (bytes_read < static_cast<ssize_t>(sizeof(header))) {
            break;
        }

        // if next record would go past end of file stop — corrupt data
        off_t next_offset =
            offset + static_cast<off_t>(sizeof(header)) + header.cmd_len + header.cwd_len;
        if (next_offset > static_cast<off_t>(file_size_)) {
            break;
        }

        offset = next_offset;
        record_count_++;
    }
}

/**
 * Deserializes one record from the mmap at the given pointer.
 * Reads the CommandRecord header, then constructs std::strings
 * for cmd and cwd from the bytes immediately following the header.
 * TODO: implement in Week 5 when building the hist CLI.
 */
ParsedRecord RecordStore::deserialize(const char* ptr) const {
    // TODO: implement when building hist CLI (Week 5)
    (void)ptr;  // suppress unused parameter warning
    return ParsedRecord{};
}

/**
 * Returns the total number of records stored in records.bin.
 */
size_t RecordStore::count() const { return record_count_; }