#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <vector>
#include "../include/kvstore.h"

#define TEST(name) void name(); std::cout << "Running " << #name << "... "; name(); std::cout << "PASSED" << std::endl;
#define ASSERT(condition) if (!(condition)) { std::cout << "assertion failed: " << #condition << "on line " << __LINE__ << std::endl; std::abort(); }

static const std::string kTestFile = "test_persistence.db";

void test_in_memory_operations() {
    // Remove test file if it exists
    std::filesystem::remove(kTestFile);
    
    KVStore store(kTestFile);
    
    // Test put and get
    store.put(1, "value1");
    ASSERT(store.get(1) == "value1");
    
    // Test remove
    store.remove(1);
    ASSERT(!store.get(1));
    
    store.put(2, "value1");
    store.put(3, "value2");
    ASSERT(store.get(2) == "value1");
    ASSERT(store.get(3) == "value2");
}

void test_persistence() {
    const std::string test_file = "test_persistence.db";
    
    // Remove test file if it exists
    std::filesystem::remove(test_file);
    
    {
        // Create a store with persistence
        KVStore store(test_file);
        
        // Add some data
        store.put(1, "value1");
        store.put(2, "value2");

    } // Destructor should also save
    
    {
        // Create a new store instance and load from the same file
        KVStore store(test_file);
        
        // Verify data was loaded
        ASSERT(store.get(1) == "value1");
        ASSERT(store.get(2) == "value2");
        
        // Modify data
        store.put(3, "value3");
        store.remove(1);
    }
    
    {
        // Create another store instance and verify changes were saved
        KVStore store(test_file);
        ASSERT(store.get(2) == "value2");
        ASSERT(store.get(3) == "value3");
        ASSERT(!store.get(1));
    }
    
    // Clean up
    std::filesystem::remove(test_file);
}

void test_concurrency() {
    KVStore store(kTestFile);
    // Print how many threads are available
    std::cout << "Number of threads available: " << std::thread::hardware_concurrency() << std::endl;
    
    std::vector<std::thread> threads;
    // time this
    auto start = std::chrono::high_resolution_clock::now();
    // n threads each doing 100000/n puts
    const int num_threads = 8;
    const int num_puts = 500000;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&store, i]() {
            for (int j = 0; j < num_puts / num_threads; j++) {
                auto k = i * num_puts / num_threads + j;
                store.put(k, "value" + std::to_string(k));
                auto read_k = (k + num_puts / 2) % num_puts;
                auto read_value = store.get(read_k);
                // if assertion will fail print expected and actual
                if (read_value && read_value != "value" + std::to_string(read_k)) {
                    std::cout << "Failed to get value for key " << read_k << std::endl;
                    std::cout << "Expected: " << "value" + std::to_string(read_k) << std::endl;
                    std::cout << "Actual: " << *read_value << std::endl;
                }
                ASSERT(!read_value || read_value == "value" + std::to_string(read_k));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Concurrent put/get operations: " << duration.count() << "ms for " 
              << num_puts * 2 << " operations with " << num_threads << " threads" << std::endl;
    std::cout << "Checking validity..." << std::endl;

    // verify that all puts went through
    for (int i = 0; i < num_puts; i++) {
        auto value = store.get(i);
        if (value != ("value" + std::to_string(i))) {
            std::cout << "Failed to get value for key " << i << std::endl;
            std::cout << "Expected: " << "value" + std::to_string(i) << std::endl;
            std::cout << "Actual: " << *value << std::endl;
        }
        ASSERT(value == ("value" + std::to_string(i)));
    }
}

int main() {
    try {
        TEST(test_in_memory_operations);
        TEST(test_persistence);
        TEST(test_concurrency);
        
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
