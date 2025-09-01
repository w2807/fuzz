#ifndef FUZZ_OPTIONS_H
#define FUZZ_OPTIONS_H

#include <string>
#include <unordered_set>

struct Options {
    std::string target;
    std::string seeds_dir;
    std::string out_dir;
    std::string dict_path;
    std::unordered_set<int> allowed_exits;
    int iterations = 10000;
    int threads = 1;
    int timeout_ms = 1000;
    int mem_mb = 0; // 0 = unlimited
    size_t max_size = 4096;
    uint64_t seed = 0; // 0 = random
};

bool parse_options(int argc, char** argv, Options& o, std::string& err);

#endif //FUZZ_OPTIONS_H
