#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <fstream>
#include <iostream>
#include <tbb/concurrent_hash_map.h>

// Uncomment this for my little group commit experiment where
// I try to avoid redundant fsyncs from concurrent puts. This
// doesn't do anything epoch-related so it's a bit weird.
// #define GROUP_COMMIT 1

class KVStore {
public:
    using K = uint32_t;
    using V = std::string;
    explicit KVStore(const std::string& persistence_file);
    ~KVStore();

    // Stores a mapping from key to value in the kvstore. Note that
    // value can be at most 4096 chars otherwise an exception will be
    // thrown.
    //
    // This is guaranteed to be durable the moment this function returns.
    // Any get after this function returns should either see the
    // given value or a later value.
    void put(K key, const V &value);

    // Gets a value mapped to K, not necessarily the most recent one.
    // Guaranteed to be within a second stale.
    std::optional<V> get(K key) const;


    // Removes the key from the map. Memory used by this key won't
    // immediately be reclaimed. Has the same semantics as put.
    void remove(K key);

private:
    static const uint32_t kTombstone = ~0;
    static const uint32_t kMaxValueSize = 4096;

    // -----------------------
    // LOG RELATED functions
    // ------------------------
    class Reader;
    void restore();
    std::optional<V> getValueFromOffset(std::streamoff offset) const;
    std::streamoff appendRecord(uint32_t checksum, K key, std::optional<std::reference_wrapper<const V>> value);
    void commitOffset(std::streamoff offset);

    bool exists(K key) const;
    void doPut(K key, std::optional<std::reference_wrapper<const V>> value);

    // store a map between key and file location data of the value
    struct StoreValue {
        // uint8_t file_id;        // not useful for now since we are only using one file
        std::streamoff offset;  // offset in file
        // uint64_t timestamp;     // timestamp, again not relevant if we are only using one file, offset does its job
        bool is_deleted;        // whether or not the value is deleted in the map
    };

    using Store_T = tbb::concurrent_hash_map<K, StoreValue>;
    Store_T store_;
    
    // Persistence file path
    std::string persistence_file_;

    // Mutex on out_fd_
    mutable std::mutex out_mutex_;
    int out_fd_;

#ifdef GROUP_COMMIT
    std::atomic<std::streamoff> max_pending_offset_{0};
    std::atomic<std::streamoff> committed_offset_{0};
    std::atomic<int> skipped_fsyncs_{0};
#endif
};
