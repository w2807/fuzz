#ifndef FUZZ_MUTATIONS_H
#define FUZZ_MUTATIONS_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct Dict {
    std::vector<std::vector<uint8_t>> tokens;
};

class Mutator {
public:
    Mutator(uint64_t seed, size_t max_size, const Dict* dict = nullptr);
    std::vector<uint8_t> mutate(const std::vector<uint8_t>& in);
    std::vector<uint8_t> crossover(const std::vector<uint8_t>& a,
                                   const std::vector<uint8_t>& b);

private:
    std::mt19937_64 rng_;
    size_t max_size_;
    const Dict* dict_;

    std::vector<uint8_t> rand_bytes(size_t n);
    std::vector<uint8_t> flip_bits(const std::vector<uint8_t>& d);
    std::vector<uint8_t> insert_bytes(const std::vector<uint8_t>& d);
    std::vector<uint8_t> delete_bytes(const std::vector<uint8_t>& d);
    std::vector<uint8_t> replace_bytes(const std::vector<uint8_t>& d);
    std::vector<uint8_t> insert_dict(const std::vector<uint8_t>& d);
};

bool load_dict(const std::string& path, Dict& d);

#endif //FUZZ_MUTATIONS_H
