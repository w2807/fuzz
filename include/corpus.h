#ifndef FUZZ_CORPUS_H
#define FUZZ_CORPUS_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Corpus {
public:
    explicit Corpus(size_t max_size_bytes, size_t max_items = 10000);
    bool load_dir(const std::string& dir);
    void add(const std::vector<uint8_t>& item, uint32_t score = 1);
    std::vector<uint8_t> pick();
    size_t size() const;
    std::vector<std::vector<uint8_t>> get_all_items() const;

private:
    struct Entry {
        std::vector<uint8_t> data;
        uint32_t score = 1;
        uint64_t picks = 0;
    };

    mutable std::mutex mu_;
    std::vector<Entry> items_;
    size_t max_size_bytes_;
    size_t cap_;
};

#endif //FUZZ_CORPUS_H
