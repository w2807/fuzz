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
    void add(const std::vector<uint8_t>& item);
    std::vector<uint8_t> pick();
    size_t size() const;

private:
    mutable std::mutex mu_;
    std::vector<std::vector<uint8_t>> items_;
    size_t max_size_bytes_;
    size_t cap_;
};

#endif //FUZZ_CORPUS_H
