#pragma once
#include <QString>

#include <list>
#include <unordered_map>
#include <utility>

namespace fincept::util {

/// A simple bounded LRU map. O(1) put/get/erase via a `std::list` ordering
/// pair list plus an `std::unordered_map<Key, list_iterator>` index.
///
/// Use this in place of a raw `QHash<K, V>` when:
///   * The map is fed values that never repeat exactly (per-symbol caches,
///     per-run agent outputs, per-request stamps) — a TTL alone won't
///     bound it because keys keep being unique.
///   * Long-session memory growth is a concern.
///   * O(log N) tree maps would be overkill (LRU lookup is amortized O(1)).
///
/// Not thread-safe; the caller owns synchronization. Designed for use on
/// the GUI thread or behind an external mutex.
template <typename Key, typename Value>
class LruMap {
  public:
    explicit LruMap(int max_size = 512) : max_size_(max_size > 0 ? max_size : 1) {}

    /// Insert or overwrite. Marks the entry as most-recently-used.
    /// Evicts the least-recently-used entry if size exceeds max_size().
    void put(const Key& key, const Value& value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = value;
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(key, value);
        index_.emplace(key, order_.begin());
        while (static_cast<int>(order_.size()) > max_size_) {
            const auto& victim = order_.back();
            index_.erase(victim.first);
            order_.pop_back();
        }
    }

    /// True if the key exists. Does NOT mark MRU.
    bool contains(const Key& key) const {
        return index_.find(key) != index_.end();
    }

    /// Returns the value if present, otherwise `default_value`. Does NOT
    /// mark MRU — use this for the cheap-lookup path where readers should
    /// not influence eviction order.
    Value peek_or(const Key& key, const Value& default_value = Value{}) const {
        auto it = index_.find(key);
        return it != index_.end() ? it->second->second : default_value;
    }

    /// Like peek_or, but promotes the entry to MRU on a hit.
    Value get_or(const Key& key, const Value& default_value = Value{}) {
        auto it = index_.find(key);
        if (it == index_.end())
            return default_value;
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    void erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return;
        order_.erase(it->second);
        index_.erase(it);
    }

    void clear() {
        order_.clear();
        index_.clear();
    }

    int size() const { return static_cast<int>(order_.size()); }
    int max_size() const { return max_size_; }

    void set_max_size(int n) {
        max_size_ = (n > 0 ? n : 1);
        while (static_cast<int>(order_.size()) > max_size_) {
            const auto& victim = order_.back();
            index_.erase(victim.first);
            order_.pop_back();
        }
    }

  private:
    int max_size_;
    std::list<std::pair<Key, Value>> order_;   // front = MRU, back = LRU
    using ListIt = typename std::list<std::pair<Key, Value>>::iterator;
    std::unordered_map<Key, ListIt> index_;
};

} // namespace fincept::util
