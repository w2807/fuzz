#include "mutations.h"

#include <algorithm>
#include <fstream>

Mutator::Mutator(uint64_t seed, size_t max_size, const Dict* dict)
    : rng(seed), max_size(max_size), dict(dict) {}

std::vector<uint8_t> Mutator::mutate(const std::vector<uint8_t>& in) {
    auto cur = in;
    const int n = (int)(rng() % 3) + 1;
    for (int k = 0; k < n; k++) {
        const int m = (int)(rng() % 5);
        switch (m) {
        case 0: cur = flip_bits(cur);
            break;
        case 1: cur = insert_bytes(cur);
            break;
        case 2: cur = delete_bytes(cur);
            break;
        case 3: cur = replace_bytes(cur);
            break;
        case 4: cur = insert_dict(cur);
            break;
        }
    }
    if (cur.size() > max_size) cur.resize(max_size);
    return cur;
}

std::vector<uint8_t> Mutator::crossover(const std::vector<uint8_t>& a,
                                        const std::vector<uint8_t>& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::uniform_int_distribution<size_t> ia(0, a.size());
    std::uniform_int_distribution<size_t> ib(0, b.size());
    size_t i = ia(rng), j = ib(rng);
    std::vector<uint8_t> res;
    res.insert(res.end(), a.begin(), a.begin() + i);
    res.insert(res.end(), b.begin() + j, b.end());
    if (res.size() > max_size) res.resize(max_size);
    return res;
}

std::vector<uint8_t> Mutator::rand_bytes(size_t n) {
    std::vector<uint8_t> r(n);
    for (size_t i = 0; i < n; i++) r[i] = (uint8_t)(rng() & 0xFF);
    return r;
}

std::vector<uint8_t> Mutator::flip_bits(const std::vector<uint8_t>& d) {
    if (d.empty()) return {0};
    auto out = d;
    std::uniform_int_distribution<size_t> di(0, out.size() - 1);
    const size_t idx = di(rng);
    std::uniform_int_distribution<int> bit(0, 7);
    out[idx] ^= (1u << bit(rng));
    return out;
}

std::vector<uint8_t> Mutator::insert_bytes(const std::vector<uint8_t>& d) {
    auto out = d;
    const size_t ins = (size_t)(rng() % 32 + 1);
    auto r = rand_bytes(ins);
    std::uniform_int_distribution<size_t> pos(0, out.size());
    out.insert(out.begin() + pos(rng), r.begin(), r.end());
    if (out.size() > max_size) out.resize(max_size);
    return out;
}

std::vector<uint8_t> Mutator::delete_bytes(const std::vector<uint8_t>& d) {
    if (d.empty()) return d;
    auto out = d;
    std::uniform_int_distribution<size_t> start(0, out.size() - 1);
    const size_t s = start(rng);
    const size_t len = (size_t)((rng() % std::min<size_t>(16, out.size() - s)) + 1);
    out.erase(out.begin() + s, out.begin() + s + len);
    return out;
}

std::vector<uint8_t> Mutator::replace_bytes(const std::vector<uint8_t>& d) {
    if (d.empty()) return rand_bytes(1);
    auto out = d;
    std::uniform_int_distribution<size_t> start(0, out.size() - 1);
    const size_t s = start(rng);
    const size_t len = (size_t)((rng() % std::min<size_t>(16, out.size() - s)) + 1);
    auto r = rand_bytes(len);
    std::copy(r.begin(), r.end(), out.begin() + s);
    return out;
}

std::vector<uint8_t> Mutator::insert_dict(const std::vector<uint8_t>& d) {
    auto out = d;
    if (!dict || dict->tokens.empty()) return insert_bytes(d);
    const size_t idx = (size_t)(rng() % dict->tokens.size());
    std::uniform_int_distribution<size_t> pos(0, out.size());
    const auto& tok = dict->tokens[idx];
    out.insert(out.begin() + pos(rng), tok.begin(), tok.end());
    if (out.size() > max_size) out.resize(max_size);
    return out;
}

bool load_dict(const std::string& path, Dict& d) {
    if (path.empty()) return false;
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        std::vector<uint8_t> tok(line.begin(), line.end());
        d.tokens.push_back(std::move(tok));
    }
    return !d.tokens.empty();
}
