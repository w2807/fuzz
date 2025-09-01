#include "mutations.h"

#include <algorithm>
#include <fstream>

Mutator::Mutator(const uint64_t seed, const size_t max_size, const Dict* dict)
    : rng_(seed), max_size_(max_size), dict_(dict) {}

std::vector<uint8_t> Mutator::mutate(const std::vector<uint8_t>& in) {
    auto cur = in;
    const int n = static_cast<int>(rng_() % 3) + 1;
    for (int k = 0; k < n; k++) {
        switch (rng_() % 5) {
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
        default: ;
        }
    }
    if (cur.size() > max_size_) {
        cur.resize(max_size_);
    }
    return cur;
}

std::vector<uint8_t> Mutator::crossover(const std::vector<uint8_t>& a,
                                        const std::vector<uint8_t>& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    std::uniform_int_distribution<size_t> ia(0, a.size());
    std::uniform_int_distribution<size_t> ib(0, b.size());
    const size_t i = ia(rng_), j = ib(rng_);
    std::vector<uint8_t> res;
    res.insert(res.end(), a.begin(), a.begin() + static_cast<ptrdiff_t>(i));
    res.insert(res.end(), b.begin() + static_cast<ptrdiff_t>(j), b.end());
    if (res.size() > max_size_) {
        res.resize(max_size_);
    }
    return res;
}

std::vector<uint8_t> Mutator::rand_bytes(const size_t n) {
    std::vector<uint8_t> r(n);
    for (size_t i = 0; i < n; i++) {
        r[i] = static_cast<uint8_t>(rng_() & 0xFF);
    }
    return r;
}

std::vector<uint8_t> Mutator::flip_bits(const std::vector<uint8_t>& d) {
    if (d.empty()) {
        return {0};
    }
    auto out = d;
    std::uniform_int_distribution<size_t> di(0, out.size() - 1);
    const size_t idx = di(rng_);
    std::uniform_int_distribution<int> bit(0, 7);
    out[idx] ^= (1u << bit(rng_));
    return out;
}

std::vector<uint8_t> Mutator::insert_bytes(const std::vector<uint8_t>& d) {
    auto out = d;
    const auto ins = (size_t)(rng_() % 32 + 1);
    auto r = rand_bytes(ins);
    std::uniform_int_distribution<size_t> pos(0, out.size());
    out.insert(out.begin() + static_cast<ptrdiff_t>(pos(rng_)), r.begin(),
               r.end());
    if (out.size() > max_size_) {
        out.resize(max_size_);
    }
    return out;
}

std::vector<uint8_t> Mutator::delete_bytes(const std::vector<uint8_t>& d) {
    if (d.empty()) {
        return d;
    }
    auto out = d;
    std::uniform_int_distribution<size_t> start(0, out.size() - 1);
    const size_t s = start(rng_);
    const size_t len = (size_t)((rng_() % std::min<size_t>(16, out.size() - s))
        +
        1);
    out.erase(out.begin() + static_cast<ptrdiff_t>(s),
              out.begin() + static_cast<ptrdiff_t>(s + len));
    return out;
}

std::vector<uint8_t> Mutator::replace_bytes(const std::vector<uint8_t>& d) {
    if (d.empty()) {
        return rand_bytes(1);
    }
    auto out = d;
    std::uniform_int_distribution<size_t> start(0, out.size() - 1);
    const size_t s = start(rng_);
    const size_t len = rng_() % std::min<size_t>(16, out.size() - s) + 1;
    auto r = rand_bytes(len);
    std::ranges::copy(r, out.begin() + static_cast<ptrdiff_t>(s));
    return out;
}

std::vector<uint8_t> Mutator::insert_dict(const std::vector<uint8_t>& d) {
    auto out = d;
    if (!dict_ || dict_->tokens.empty()) {
        return insert_bytes(d);
    }
    const auto idx = rng_() % dict_->tokens.size();
    std::uniform_int_distribution<size_t> pos(0, out.size());
    const auto& tok = dict_->tokens[idx];
    out.insert(out.begin() + static_cast<ptrdiff_t>(pos(rng_)), tok.begin(),
               tok.end());
    if (out.size() > max_size_) {
        out.resize(max_size_);
    }
    return out;
}

bool load_dict(const std::string& path, Dict& d) {
    if (path.empty()) {
        return false;
    }
    std::ifstream ifs(path);
    if (!ifs) {
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }
        std::vector<uint8_t> tok(line.begin(), line.end());
        d.tokens.push_back(std::move(tok));
    }
    return !d.tokens.empty();
}
