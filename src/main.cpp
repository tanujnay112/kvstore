#include <iostream>
#include <string>
#include "../include/kvstore.h"

void printHelp() {
    std::cout << "KV Store Commands:\n"
              << "  put <key> <value> - Store a key-value pair\n"
              << "  get <key>         - Retrieve a value by key\n"
              << "  del <key>         - Delete a key-value pair\n"
              << "  help              - Show this help message\n"
              << "  exit              - Exit the program\n";
}

int main(int argc, char* argv[]) {
    // TODO
    std::string cmd;
    KVStore store("test.db");
    
    while (true) {
        std::cout << "> ";
        std::cin >> cmd;
        
        if (cmd == "exit") {
            break;
        } else if (cmd == "help") {
            printHelp();
        } else if (cmd == "put") {
            KVStore::K key;
            KVStore::V value;
            std::cin >> key >> value;
            store.put(key, value);
        } else if (cmd == "get") {
            KVStore::K key;
            std::cin >> key;
            if (auto opt = store.get(key)) {
                std::cout << *opt << std::endl;
            } else {
                std::cout << "(nil)\n";
            }
        } else if (cmd == "del") {
            KVStore::K key;
            std::cin >> key;
            store.remove(key);
        } else {
            std::cout << "Unknown command. Type 'help' for more information.\n";
        }
    }

}
