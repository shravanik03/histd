# Design

## Problem

Bash's built-in history is a flat, append-only text file with no structure — no way to search by keyword, filter by exit code, filter by directory, or find patterns in repeated failures. `histd` captures every command with rich metadata (exit code, duration, working directory, timestamp) and makes it genuinely searchable.

## Architecture Overview

```
shell hook (histd.plugin.bash)
      │  fire-and-forget over Unix domain socket
      ▼
histd daemon (long-running background process)
      │  pwrite() → records.bin (binary, append-only)
      │  in-memory InvertedIndex (unordered_map)
      │  periodic + on-exit flush → index.bin (mmap-friendly binary format)
      ▼
hist CLI (short-lived, one process per invocation)
      │  reads records.bin + index.bin directly (no IPC with daemon)
      ▼
terminal output (colored, ranked results)
```

## Key Decisions

### Binary format over text

`records.bin` stores each command as a fixed 24-byte header (timestamp, duration, exit code, session hash, cmd length, cwd length) followed by the raw command and cwd bytes. Text formats require scanning byte-by-byte to find record boundaries; a fixed header lets any reader jump directly from one record to the next using simple offset arithmetic, and lets fields be read without string parsing.

### mmap over read()/fread()

Both `records.bin` and `index.bin` are read via `mmap()` rather than buffered reads. This avoids the double-copy of disk → kernel buffer → user buffer that traditional I/O requires, and lets the OS lazily page in only the bytes actually touched. For a search that only needs to look up a handful of terms out of thousands, this means we never pay for data we don't access.

### Append-only, no mutex

The daemon is the sole writer of `records.bin`, using `pwrite()` at a known offset (current end of file) for every new record — writes never overlap or modify existing bytes. Readers (the CLI, or the daemon's own catch-up scan) either see a stable snapshot via `mmap()` at the moment they map the file, or safely stop early if they encounter a record whose declared length would run past the mapped file size (guards against reading a torn/in-progress write). This design needs no locking because there is exactly one writer, writes are strictly additive, and readers tolerate a slightly-stale view without corruption.

### FNV-1a hashing over std::hash

Session IDs (`bash-<pid>`) are hashed to a fixed 4-byte value for storage in the binary record header. `std::hash` is explicitly *not* guaranteed to be stable across process runs by the C++ standard — using it would make session filtering unreliable across daemon restarts. FNV-1a is a simple, fast, deterministic hash implemented directly (offset basis `2166136261`, prime `16777619`), guaranteeing the same string always hashes identically, run after run.

### TF-IDF with recency boost, not plain keyword match

Each query token's relevance is scored by inverse document frequency — rare tokens (like a specific flag or hostname) score higher than common ones (like `git` or `cd`, which appear constantly and thus carry less discriminating power). On top of the base IDF score, a recency multiplier (`1 + 1/(days_ago + 1)`) favors more recent commands, since a search is far more likely to be looking for something done recently than something from months ago. `cd` and `pwd` are filtered entirely from indexing (pure navigation, no search value), while common shell tools like `ls`, `sudo`, and `echo` are deliberately *kept*, since real searches do reference them (e.g. "sudo nginx restart").

### Byte offsets instead of sequential record IDs

Postings in the inverted index reference records by their exact byte offset in `records.bin`, not a sequential integer ID. This makes lookup O(1) — `store.read_at(offset)` jumps directly via `mmap_ptr + offset` and deserializes on the spot — with zero scanning, regardless of how large the history has grown.

### Two-pointer intersection for AND-logic search

Posting lists for each query token are kept sorted by byte offset. Finding records that match *all* query tokens is done via classic two-pointer list intersection (O(a + b) per pair of lists), rather than any hashing or set-based approach, since the lists are already sorted as a natural consequence of records being appended (and thus indexed) in increasing offset order.

### Persistent, memory-mapped index (`index.bin`)

Without persistence, every `hist search` invocation would need to re-scan and re-tokenize the *entire* `records.bin` from scratch — an O(N) cost that grows with history size and directly hurts the tool's core promise of being fast. Instead:

- The **daemon** maintains a live `InvertedIndex` in RAM, updated incrementally as each new record arrives (`add_record()`), and periodically serializes it to `index.bin` (every 50 records, and always on clean shutdown).
- `index.bin`'s binary layout is a fixed header, a **sorted term directory** (keyed by FNV-1a hash, enabling binary search), a packed term-strings section, and a packed postings section — all fixed-size or offset-addressed, designed for direct `mmap()` access with zero deserialization.
- The **CLI** checks whether `index.bin`'s recorded `last_offset` exactly matches the current size of `records.bin`. If fresh, it mmaps `index.bin` and does an O(log N) binary search per query token directly against the file — no rebuild, no full read. If stale (records have been added since the last daemon flush) or missing, it transparently falls back to the O(N) in-memory rebuild path, guaranteeing correctness at the cost of speed in that specific case.

Measured impact: at 10,000 records, the mmap path resolves a search in ~20ms versus ~500-560ms for a full rebuild — roughly a 25-28x speedup (see `BENCHMARKS.md`), and the gap widens as history grows, since the mmap path's cost is dominated by O(log N) lookups while the rebuild path is strictly O(N).

### Why two independent code paths share one algorithm core

The in-memory search (daemon's live index, and the CLI's fallback-rebuild path) and the mmap-based search (CLI's fast path) operate on fundamentally different underlying representations — a `std::unordered_map<std::string, std::vector<Posting>>` versus raw `DiskPosting` arrays reached via pointer arithmetic into an `mmap`'d file. Rather than duplicating the intersection and ranking logic for each, both paths convert their postings into a small storage-agnostic `PostingView` struct and delegate to one shared `intersect_views()` / `rank_top_k()` implementation. This keeps the actual algorithm (two-pointer intersection, IDF scoring, recency-boosted top-K ranking via a min-heap) written and tested exactly once.

## Known Limitations & Trade-offs

- **CLI and daemon do not communicate directly.** The CLI reads `records.bin`/`index.bin` from disk rather than querying the daemon over the socket. This avoids needing a request/response protocol, but means the CLI's view of the index can lag the daemon's live in-memory state by up to the flush interval (50 records, or until the next clean shutdown). A future version could add a query protocol so the daemon serves searches directly from its always-current in-memory index, eliminating the fallback-rebuild path entirely — at the cost of the daemon becoming a hard dependency for search (currently, `hist` still works even if the daemon isn't running, by rebuilding from `records.bin` directly).
- **No `flock()` or explicit file locking.** Correctness currently relies on the append-only write pattern and the readers' tolerance for a slightly-stale mmap snapshot, rather than explicit locking. This has been verified safe under burst-load stress testing (60+ concurrent record submissions), but a stricter production system might add advisory locking as defense in depth.
- **Delimiter choice.** Records are field-delimited using the ASCII Unit Separator (`\x1f`) rather than a printable character like `|`, specifically because command text frequently contains pipes (`ls -la | grep foo` is a completely ordinary command) — an earlier version of this project used `|` and broke on exactly this case, caught via stress testing (see commit history).
- **Daemon restart cost.** On startup, the daemon does a one-time full scan of `records.bin` to rebuild its in-memory index (rather than deserializing `index.bin` back into the map), since `IndexReader` only supports mmap-based binary search lookup, not full deserialization into a hash map. This is a deliberate simplification — daemon restarts are rare (once per login/boot), so paying an O(N) cost once is an acceptable trade-off versus writing a second deserialization code path.

## Future Enhancements

- Daemon-served search over the existing Unix socket (bidirectional request/response protocol), removing the CLI's dependency on `index.bin` freshness entirely.
- Segment-based incremental indexing (Lucene-style), replacing whole-file `index.bin` rewrites with small append-only delta segments merged periodically — would reduce flush cost from O(index size) to O(new records) per flush.
- Semantic/embedding-based search to catch conceptually related but lexically different queries (e.g. "container" surfacing `docker` commands).
- A dedicated `histd-send` client binary to replace `nc` in the shell hook, removing the small process-spawn overhead per command.
