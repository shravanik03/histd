/**
 * daemon/main.cpp
 *
 * histd — Smart Terminal History Engine
 *
 * The main daemon process. Runs in the background, listens on a Unix
 * domain socket, receives command records from shell hooks, and writes
 * them to disk for indexing and search.
 *
 * Lifecycle:
 *   1. Parse paths from $HOME
 *   2. Create data directory
 *   3. Register signal handlers
 *   4. Daemonize (fork + setsid + redirect I/O)
 *   5. Write PID file
 *   6. Open Unix socket
 *   7. Run epoll event loop until SIGTERM/SIGINT
 *   8. Cleanup and exit
 */

#include <fcntl.h>       // for O_RDWR, open
#include <signal.h>      // signal, SIGINT, SIGTERM
#include <sys/epoll.h>   // epoll_create1, epoll_ctl, epoll_wait
#include <sys/socket.h>  // socket, bind, listen, accept, send, recv
#include <sys/stat.h>    // umask
#include <sys/un.h>      // struct sockaddr_un
#include <unistd.h>      // fork, setsid, close, read, write

#include <atomic>      // For std::atomic<bool>
#include <cerrno>      // errno
#include <cstring>     // for strerror and memset
#include <filesystem>  // for std::filesystem create directories
#include <fstream>     // for std::ofstream
#include <iostream>    // for std::cerr
#include <string>      // for std::string

#include "index_store.hpp"
#include "inverted_index.hpp"
#include "record_store.hpp"  // for RecordStore
#include "tokenizer.hpp"

std::string HOME_PATH;
std::string SOCKET_PATH;
std::string PID_FILE;
std::string DATA_DIR;
std::atomic<bool> running{true};

/**
 * Handle SIGINT and SIGTERM
 * When any of these signals are received, set running to false
 * indicating the daemon should exit.
 */
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = false;
    }
}

/**
 * Step 1: Fork the process
 * Step 2: Exit the parent process
 * Step 3: Create a new session (setsid) detaching from terminal
 * Step 4: Reset file creation mask (umask)
 * Step 5: Redirect stdin/stdout/stderr to /dev/null
 */
void daemonize() {
    pid_t pid = fork();
    if (pid > 0) {
        exit(0);
    }

    setsid();
    umask(0);

    int dev_null = open("/dev/null", O_RDWR);
    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);

    close(dev_null);
}

/**
 * Write the process ID to a file for later use
 * 1. You can stop the daemon later by reading the PID from the file
 * 2. On startup, you can check if the daemon is already running
 */
void write_pid_file(const std::string& path) {
    std::ofstream file(path);
    file << getpid() << "\n";
}

/**
 * Setup a Unix domain socket
 * 1. Unlink the socket if it already exists
 * 2. Create a socket
 * 3. Bind the socket to a path
 * 4. Listen on the socket
 */
int setup_socket(const std::string& path) {
    unlink(path.c_str());
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        exit(1);
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        exit(1);
    }

    if (listen(socket_fd, 128) < 0) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        exit(1);
    }

    return socket_fd;
}

/**
 * Parses a raw pipe-delimited record string received from the shell hook
 * and appends it to the RecordStore as a binary CommandRecord on disk.
 *
 * Expected format: cmd|cwd|exit_code|duration_ms|timestamp|session_id
 */
void handle_record(const std::string& record, RecordStore& store, InvertedIndex& index,
                   const Tokenizer& tokenizer, const std::string& index_path,
                   uint32_t& records_since_flush) {
    // parse pipe-delimited record into a ParsedRecord
    // fields: cmd|cwd|exit_code|duration_ms|timestamp|session_id

    // make a mutable copy to parse
    std::string remaining = record;
    ParsedRecord rec;

    // helper lambda to extract next field
    // finds next | returns everything before it
    // advances remaining past the |
    auto next_field = [&]() -> std::string {
        size_t pos = remaining.find('|');
        if (pos == std::string::npos) {
            std::string field = remaining;
            remaining = "";
            return field;
        }
        std::string field = remaining.substr(0, pos);
        remaining = remaining.substr(pos + 1);
        return field;
    };

    rec.cmd = next_field();
    rec.cwd = next_field();
    rec.exit_code = std::stoi(next_field());
    rec.duration_ms = static_cast<uint32_t>(std::stoul(next_field()));
    rec.timestamp = std::stoull(next_field());
    rec.session_id = next_field();

    uint64_t offset = store.append(rec);
    if (offset == UINT64_MAX) return;  // append failed, skip indexing

    index.add_record(rec, offset, tokenizer);
    records_since_flush++;

    // periodic flush — every 50 records
    if (records_since_flush >= 50) {
        uint64_t current_size = offset + sizeof(CommandRecord) + rec.cmd.size() + rec.cwd.size();
        IndexSerializer::save(index, index_path, current_size);
        records_since_flush = 0;
    }
}

/**
 * Run the epoll event loop
 * 1. Create an epoll instance
 * 2. Add the listening socket to the epoll instance
 * 3. Run the event loop
 *   - If a new connection is accepted, add it to the epoll instance
 *   - If a client sends data, read it and handle the record
 * 4. Close the epoll instance
 */
void run_event_loop(int server_fd, RecordStore& store, InvertedIndex& index,
                    const Tokenizer& tokenizer, const std::string& index_path,
                    uint32_t& records_since_flush) {
    int epfd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);

    struct epoll_event events[10];

    while (running) {
        int n = epoll_wait(epfd, events, 10, 1000);

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                // new connection from client
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) {
                    continue;
                }

                struct epoll_event client_ev;
                client_ev.events = EPOLLIN;
                client_ev.data.fd = client_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &client_ev);
            } else {
                // client sent data
                char buffer[4096] = {0};
                int client_fd = events[i].data.fd;
                ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);

                if (bytes > 0) {
                    std::string record(buffer, bytes);
                    if (!record.empty() && record.back() == '\n') {
                        record.pop_back();
                    }
                    handle_record(record, store, index, tokenizer, index_path, records_since_flush);
                }

                close(client_fd);
            }
        }
    }

    close(epfd);
}

/**
 * Entry point for the histd daemon.
 *
 * Initializes paths, creates the data directory, registers signal
 * handlers, daemonizes the process, opens the Unix socket, and
 * enters the epoll event loop. Cleans up on exit.
 *
 * @return 0 on clean exit, 1 on fatal error
 */
int main() {
    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << "HOME environment variable not set" << std::endl;
        exit(1);
    }

    HOME_PATH = home;
    SOCKET_PATH = HOME_PATH + "/.local/share/histd/histd.sock";
    PID_FILE = HOME_PATH + "/.local/share/histd/histd.pid";
    DATA_DIR = HOME_PATH + "/.local/share/histd";

    // Create data directory before daemonizing so errors are visible
    // After daemonize(), stderr is redirected to /dev/null
    std::filesystem::create_directories(DATA_DIR);

    // SIGINT  = Ctrl+C from terminal (during development)
    // SIGTERM = kill command (normal shutdown in production)
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    daemonize();

    write_pid_file(PID_FILE);

    int server_fd = setup_socket(SOCKET_PATH);

    // create RecordStore in WRITE mode
    // lives for the entire daemon lifetime
    RecordStore store(DATA_DIR + "/records.bin", Mode::WRITE);

    std::string index_path = DATA_DIR + "/index.bin";
    InvertedIndex index;
    Tokenizer tokenizer;
    uint32_t records_since_flush = 0;

    // startup catch-up: rebuild in-memory index from records.bin
    // (Option 1 — full scan on daemon startup only, not on every search)
    {
        RecordStore read_store(DATA_DIR + "/records.bin", Mode::READ);
        index.build(read_store, tokenizer);
    }

    run_event_loop(server_fd, store, index, tokenizer, index_path, records_since_flush);

    IndexSerializer::save(index, index_path, store.file_size());
    close(server_fd);
    unlink(SOCKET_PATH.c_str());
    std::filesystem::remove(PID_FILE);

    return 0;
}
