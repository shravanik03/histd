# Benchmarks

All numbers measured on a release build (`cmake .. -DCMAKE_BUILD_TYPE=Release` equivalent, no sanitizers) on the development machine (Ubuntu, GNU 13.3.0).

## Search Latency

| Records | Fast path (mmap index.bin) | Fallback (full rebuild from records.bin) | Speedup |
|---------|------------------------------|--------------------------------------------|---------|
| 1,000   | ~10-20ms                     | ~70-90ms                                    | ~5x     |
| 10,000  | ~20ms                        | ~500-560ms                                  | ~25-28x |

The fast path uses a memory-mapped, sorted term directory with O(log N) binary search lookup per query token. The fallback path re-scans and re-tokenizes every record in `records.bin`, an O(N) operation. As history grows, the fast path's near-flat latency diverges further from the fallback's linear growth — the gap widens from ~5x at 1K records to ~28x at 10K records.

## Daemon Memory Footprint

| State                                    | RSS      |
|-------------------------------------------|----------|
| Idle, 10,000 records indexed in RAM       | ~60MB    |
| Transient peak during 40K-record burst ingest | ~280MB (temporary, settles back to steady-state within seconds of load stopping) |

Memory was confirmed stable (not leaking) by sampling RSS at 30-second intervals during idle periods — no growth observed.

## Shell Hook Overhead

Measured by timing 100 invocations of a no-op command (`true`) with and without the `histd_preexec`/`histd_precmd` hooks active.

| Condition        | Total (100 runs) | Per-command |
|-------------------|-------------------|-------------|
| Hooks active      | ~5ms              | ~0.05ms     |
| Hooks disabled    | ~3ms              | ~0.03ms     |

Difference is within measurement noise — confirms the fire-and-forget design (backgrounded `nc` send with `disown`) adds no perceptible latency to interactive shell use.

## File Sizes

| Records | records.bin | index.bin |
|---------|-------------|-----------|
| 1,000   | ~77 KB      | ~172 KB   |
| 10,000  | ~730 KB     | ~1.6 MB   |

`index.bin` is roughly 2.2x the size of `records.bin` due to the term directory, term strings, and posting list overhead of the on-disk inverted index format.

## Methodology Notes

- Synthetic benchmark data was generated via `scripts/generate_synthetic.sh`, which sends records directly over the daemon's Unix socket (bypassing the shell hook) to allow generating large volumes quickly. Generation itself is bottlenecked by spawning one `nc` process per record (~5ms/record), which is a test-harness limitation, not a reflection of daemon throughput.
- All latency numbers were measured with AddressSanitizer disabled, since ASan instrumentation adds significant overhead (~2x observed) unrelated to real-world performance.
- The "fast path" requires `index.bin`'s recorded `last_offset` to exactly match the current size of `records.bin` (i.e., no new records since the last daemon flush). If stale, the CLI transparently falls back to a full rebuild for correctness.