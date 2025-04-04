#include "kvstore.h"
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

KVStore::KVStore(const std::string& persistence_file) 
    : persistence_file_(persistence_file) {
    restore();
}

KVStore::~KVStore() {}

// Helper class to read record components from a file.
class KVStore::Reader {
 public:
    Reader(std::ifstream& file) : file_(file) {
        // This can be more efficient by reading file metadata.
        file_.seekg(0, std::ios::end);
        file_size_ = file_.tellg();
        file_.seekg(0, std::ios::beg);
    }
    bool hasNext() const { return file_.tellg() < file_size_; }
    bool readInt(uint32_t& value) {
        file_.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (file_.gcount() != sizeof(value)) {
            return false;
        }
        return true;
    }
    bool read(V& value) {
        file_.read(&value[0], value.size());
        if (file_.gcount() != value.size()) {
            return false;
        }
        return true;
    }
 private:
    std::ifstream& file_;
    size_t file_size_;
};

// Helper function to compute a simple hash for a key-value pair.
static uint32_t make_checksum(KVStore::K key, uint32_t value_length, std::optional<std::reference_wrapper<const KVStore::V>> value) {
    std::hash<KVStore::K> key_hash;
    std::hash<uint32_t> value_length_hash;
    std::hash<KVStore::V> value_hash;
    // Simple hash for now, does not need to be error correcting.
    return key_hash(key) ^ value_length_hash(value_length) ^ (value ? value_hash(value->get()) : 0);
}


// Helper function to restore the store from the persistence file. Skips any corrupted entries
// and truncates the file to exclude such corrupt entries.
void KVStore::restore() {
    // read persistence_file_ and load into store_
    std::streamoff valid_pos = 0;
    if (std::filesystem::exists(persistence_file_)) {
        // create file if it isn't there, if it is there then don't truncate
        std::ifstream file(persistence_file_, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open persistence file");
        }
        
        K key;
        V value;
        uint32_t persisted_checksum;

        Reader reader(file);
        while (reader.hasNext()) {
            if (!reader.readInt(persisted_checksum)) {
                std::cout << "Bad record at " << valid_pos << std::endl;
                break;
            }
            if (!reader.readInt(key)) {
                std::cout << "Bad record at " << valid_pos << std::endl;
                break;
            }
            auto value_offset = file.tellg();

            uint32_t value_length;
            if (!reader.readInt(value_length)) {
                std::cout << "Bad record at " << valid_pos << std::endl;
                break;
            }

            if (value_length != kTombstone) {
                if (value_length > kMaxValueSize) {
                    std::cout << "Bad record at " << valid_pos << std::endl;
                    break;
                }

                value.resize(value_length);
                if (!reader.read(value)) {
                    std::cout << "Bad record at " << valid_pos << std::endl;
                    break;
                }
                // Do checksum verification
                if (persisted_checksum != make_checksum(key, value_length, value)) {
                    std::cout << "Bad record at " << valid_pos << std::endl;
                    break;
                }
            } else {
                if (persisted_checksum != make_checksum(key, kTombstone, std::nullopt)) {
                    std::cout << "Bad record at " << valid_pos << std::endl;
                    break;
                }
                store_.erase(key);
            }

            Store_T::accessor acc;
            store_.insert(acc, key);
            // acc->second.file_id = 0;
            acc->second.offset = value_offset;
            // acc->second.timestamp = 0;
            acc->second.is_deleted = value_length == kTombstone;
            valid_pos = file.tellg();
        }
    }

    // truncate file to valid_pos
    std::ofstream(persistence_file_, std::ios::binary | std::ios::out | std::ios::in).seekp(valid_pos);
    
    // this helps roll back to the end of the last good record
    // Note that this only works if the file was appended to by
    // a single writer. Otherwise, we are potentially rolling back
    // committed records here.
    out_fd_ = open(persistence_file_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (out_fd_ == -1) {
        throw std::runtime_error("Failed to open persistence file");
    }

#ifdef GROUP_COMMIT
    committed_offset_ = valid_pos;
#endif
}

// ----------------------------------------------
// LOG FILE RELATED FUNCTIONS
// ----------------------------------------------

static void update_max_offset(std::atomic<std::streamoff> &aggregator, std::streamoff new_val) {
    std::streamoff old_agg;
    do {
        old_agg = aggregator.load(std::memory_order_acquire);
    } while (old_agg < new_val &&
             !aggregator.compare_exchange_weak(old_agg, new_val,
                                               std::memory_order_release,
                                               std::memory_order_relaxed));
}

// Helper function to append a record to the persistence file.
std::streamoff KVStore::appendRecord(uint32_t checksum, K key, std::optional<std::reference_wrapper<const V>> value) {
    std::ostringstream outstream;
    outstream.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    outstream.write(reinterpret_cast<const char*>(&key), sizeof(key));
    auto kv_offset = 0;
    if (!value) {
        uint32_t tombstone = kTombstone;
        outstream.write(reinterpret_cast<const char*>(&tombstone), sizeof(tombstone));
    } else {
        uint32_t value_size = value->get().size();
        outstream.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
        outstream.write(value->get().c_str(), value_size);
    }

    {
        // Would be cool if there was a version of write that appended but also returned
        // the offset at which it was written. Would not need this lock then. I could
        // technically avoid the call to lseek to get the offset and keep track of it in
        // user-space here but I would still need the lock to make sure the kv_offset I see
        // is indeed the offset I wrote my data to.
        std::lock_guard<std::mutex> lock(out_mutex_);
        kv_offset = lseek(out_fd_, 0, SEEK_END);
        int res = write(out_fd_, outstream.str().c_str(), outstream.str().size());
        if (res != outstream.str().size()) {
            throw std::runtime_error("Failed to write to persistence file, must fail");
            // TODO: We can fix up the file here if it was a partial write.
        }
    }

#ifdef GROUP_COMMIT
    update_max_offset(max_pending_offset_, kv_offset);
#endif

    return kv_offset + sizeof(uint32_t) + sizeof(K);
}

// Commit a log offset to disk.
void KVStore::commitOffset(std::streamoff offset) {
#ifdef GROUP_COMMIT
    if (offset <= committed_offset_) {
        skipped_fsyncs_++;
        return;
    }

    auto pending_commit_offset = max_pending_offset_.load(std::memory_order_acquire);
#endif

    fsync(out_fd_);

#ifdef GROUP_COMMIT
    update_max_offset(committed_offset_, pending_commit_offset);
#endif
}

// Helper function to read a value from the persistence file from an offset.
// This offset is expected to point to the length portion of the value that precedes the
// actual value data.
std::optional<KVStore::V> KVStore::getValueFromOffset(std::streamoff offset) const {
    std::ifstream in(persistence_file_, std::ios::binary);
    in.seekg(offset);
    uint32_t value_length;
    in.read(reinterpret_cast<char*>(&value_length), sizeof(value_length));
    
    // Check if it's a tombstone
    if (value_length == kTombstone) {
        return std::nullopt;
    }
    
    V value;
    value.resize(value_length);
    in.read(&value[0], value_length);
    return value;
}

// ----------------------------------------------------------------------------------------

// Helper function to commit a key-value pair to the database.
void KVStore::doPut(K key, std::optional<std::reference_wrapper<const V>> value) {
    uint32_t value_size = value ? value->get().size() : kTombstone;
    auto checksum = make_checksum(key, value_size, value);
    auto value_offset = appendRecord(checksum, key, value);
    {
        Store_T::accessor acc;
        if (store_.find(acc, key)) {
            if (acc->second.offset < value_offset) {
                acc->second.offset = value_offset;
            }
            // Otherwise someone else appended to the log after us and updated
            // the store_. Let's respect the log's ordering.
        } else {
            store_.insert(acc, key);
            // acc->second.file_id = 0;
            acc->second.offset = value_offset;
            // acc->second.timestamp = 0;
            acc->second.is_deleted = value ? false : true;
        }
    }

    commitOffset(value_offset);
    // fsync(out_fd_);
    // TODO: need to look at resizing logic
}

// ----------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------


// Public API to store a key-value pair.
void KVStore::put(K key, const V &value) {
    if (value.size() > kMaxValueSize) {
        throw std::runtime_error("Value size exceeds maximum allowed");
    }
    doPut(key, value);
}

// Public API to retrieve a value by key.
std::optional<KVStore::V> KVStore::get(K key) const {
    uint32_t val_offset = 0;
    {
        Store_T::const_accessor acc;
        if (!store_.find(acc, key) || acc->second.is_deleted) {
            return std::nullopt;
        }
        val_offset = acc->second.offset;
    }
    return getValueFromOffset(val_offset);
}

bool KVStore::exists(K key) const {
    Store_T::const_accessor acc;
    return store_.find(acc, key) && !acc->second.is_deleted;
}

// Public API to remove a key from the store. Does not immediately reclaim any
// space.
void KVStore::remove(K key) {
    // There are two main ways to go about this. Either write a tombstone tuple or
    // do an in-place erase of the key from the store_. The first option is easy to
    // reason about while allowing for concurrency at the cost of extra memory and
    // storage. The second option would be more space-efficient but be more complex
    // to implement correctly. If a remove is called concurrently with a put on the same
    // key and the key's log message ends up before the tombstone, then care must be taken
    // to make sure that the put does not write its entry to store_ after the key is erased
    // from store_ by the call to remove(). One solution to this would be to hold a mutex
    // on the data file from file append to when store_.erase is called. This would
    // block other put and remove operations.
    // For now, we go with the in-memory tombstone approach. One can extend this design
    // to periodically have a background worker get rid of all tombstones from the store_
    // that have a file offset < P where P is the some committed offset near the end of the file.

    if (exists(key)) {
        // Could be the case that we append a redundant tombstone entry into the WAL. It could
        // also be the case that the tuple detected by the exists call is different from the tuple
        // deleted by the following line but that's fine.
        doPut(key, std::nullopt);
    }

    // If the key didn't exist then it's as if we executed the remove at the timepoint exists(key)
    // read at.
}
