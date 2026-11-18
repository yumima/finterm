#pragma once
// RetiredStorage.h — removes storage the app no longer reads.
//
// When a dataset moves to one owner, or a cache's format or key is bumped,
// the files, directories and settings groups it leaves behind are never read
// again and never cleaned — 437 MB of unreachable parquet panels were found
// this way. Superseded FILES are pruned by their owner, where the name scheme
// lives (DiskCache::prune; the Python caches' keep_days). This sweep holds
// what has no owner left: whole retired directories and settings groups.
// cache.db keys need no entry: every key carries a TTL and CacheDatabase
// deletes expired rows at startup.

namespace finterm::storage {

/// Delete retired cache directories and dead settings groups.
/// Idempotent and cheap once done; safe to run on every launch.
void sweep_retired_storage();

} // namespace finterm::storage
