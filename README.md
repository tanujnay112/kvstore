# C++ Key-Value Store

A simple, thread-safe persistent key-value store implementation in C++.

## Features

- Simple command-line interface
- Ability to concurrently get/put/remove in a persisted manner.

## Building the Project
This project requires Intell TBB to build. On Mac first do the following
```
brew install tbb
```
Next use CMake to build. To build:

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

The public API is in kvstore.h. You can programmatically use this or use the REPL
that can be accessed via the `kvstore` binary. The programmatic API is as follows:

### Commands

Once running, you can use the following commands:

- `put <key> <value>` - Store a key-value pair
- `get <key>` - Retrieve a value by key
- `del <key>` - Delete a key-value pair
- `help` - Show help message
- `exit` - Exit the program

#

## Running Tests

```bash
cd build
./kvstore_test
```
## Design
The overall design of the KVStore aims to be simple and thread-safe. Its main purpose is to provide a simple guarantee that any operation that the KVStore API acknowledges or returns from is durable and persisted across crashes/restarts. It also provides the guarantee that at all times a consistent view of the store is visible. More specifically, each call to get() pulls from a view of the database that's consistent with a particular offset in the log. More specifically, if remove(k) is ordered before put(k,v) in the log, the get(k) should eventually yield v if nothing else had updated k.

KVStore achieves data persistence on concurrent writes by recording each key-value pair linearly to a single file that serves as the main source of persisted data for the KVStore. The file's layout is as follows:

| checksum1 | key1 | len(value1) | value1 | checksum2 | key2 | len(value2) | value2 | ...

where:
- checksumx: A simple hash checksum of the rest of the bytes of this record. This allows us to verify the integrity of the record and detect corruption that may occur during persistence due to things like power loss in the middle of a write. This is just a simple hash checksum as opposed anything more sophisticated so it is more liable to miss corruptions than industry standards.
- keyx: The 32-bit integer key in binary format
- len(valuex): The length of the value string in binary format as a 32-bit integer (can be optimized to just be 12 bits). This field can also be equal to kTombstone (0xFFFFFFFF) to indicate a tombstone entry for this key.
- valuex: The value string

If we had multiple files, we would also need a timestamp to be recorded to serialize records across files but that's not the case here.

During startup, the KVStore will load the data from the persistence file into an in-memory hash table. The checksums are used to detect any corruption in the file. If a corruption is detected on an entry, the KVStore will skip that entry and truncate the file to the end of the last good record. This is predicated under the assumption that only the last record can be corrupted. This can only be true if there is a single synchronous writer to the file.

The hash table is a concurrent hash table that allows concurrent reads and writes. It maps keys to offsets in the persistence file. Care is taken to make sure that any updates to a key's offset value happens in strictly increasing order to maintain consistency with the data file's ordering. The hash table is a concurrent hash table from Intel's Thread Building Blocks library that handles dynamic resizing.

This design satisfies the requirements but also makes a few major tradeoffs and assumptions in doing so:
1. Strict durability: The fact that we mandate put calls to be immediately durable upon return means that either we do one fsync per put call or we buffer up put calls within an epoch to do a singular fsync. The latter would work far better and allow for higher throughput under write-heavy workloads but add unnecessary delays to writes in read-heavy workloads. The former is also much simpler to implement. It is also to be noted that this simplistic implementation's higher number of fsyncs sacrifices the lifetime of any given SSD.

2. Single log file: We maintain a single write-ahead log file to protect our KVStore. Having multiple WAL's would enable greater parallelism both during bootup restore time and for puts and allow for higher throughput for all the API calls. This would also take up more memory per record as each record now would need to be associated with a monotonically increasing timestamp that helps order records across the various WAL files. With our singular WAL file, the offset at which a record's value is stored in the file performs the same function as the aforementioned timestamp. The single file version we picked allows for a simpler implementation and lower memory usage.

3. Random reads: This read-path of this design is suited for an SSD-based system due to the fact that random reads are done without much caching. Higher random read latencies and lower read parallelism on HDD's would necessitate the need for some page-cache or read-batching which is not considered in this implementation.

While a large part of this design values simplicity, the code is also written to be extensible to more efficient designs. For example, the in-memory keydir map's Value structure has been formatted to easily allow us to add more fields such as timestamp or file id in the future.

It is also simple to think about the concurrency semantics of this system. Each call to get() can be thought of as a view into the database as of some offset, P, in the log file. Repeated calls to get() would view strictly nondecreasing log file offsets. It is important this semantic of get() to be consistent with the other API methods, put() and remove(). If a get() call views that database at log offset P, subsequent put() and remove() calls must behave as if they were applied at a log offset > P. For put() this is enforced by the fact that the put() calls appends a new log entry and updates the in-memory map accordingly, visibly overwriting any previous entry on k. Care is taken to make sure that such a put(k,v) call does not update the map at k if there is already a mapping for k at a later log offset. This can happen in case there is another concurrent put/remove. remove() calls behave like put() calls except they add tombstone entries to the map and log.

There is also a file with a few basic test in tests/kvstore_test.cpp