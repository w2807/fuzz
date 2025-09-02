#include "corpus.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "logger.h"

Corpus::Corpus(const size_t max_size_bytes, const size_t max_items) :
    max_size_bytes_(max_size_bytes), cap_(max_items) {
}

bool Corpus::load_dir(const std::string& dir) {
    size_t n = 0, skipped = 0;
    for (std::error_code ec;
         auto& p : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!p.is_regular_file()) {
            continue;
        }
        std::ifstream ifs(p.path(), std::ios::binary);
        std::vector<uint8_t> b((std::istreambuf_iterator(ifs)), {});
        if (b.empty()) {
            skipped++;
            continue;
        }
        if (b.size() > max_size_bytes_) {
            b.resize(max_size_bytes_);
        }
        add(b);
        n++;
    }
    logx::info("loaded seeds: " + std::to_string(n) +
               " skipped: " + std::to_string(skipped));
    if (size() == 0) {
        std::vector<uint8_t> def = {'s', 'e', 'e', 'd'};
        add(def);
    }
    return size() > 0;
}

void Corpus::add(const std::vector<uint8_t>& item) {
    std::lock_guard lk(mu_);
    if (items_.size() >= cap_) {
        return;
    }
    items_.push_back(item);
}

std::vector<uint8_t> Corpus::pick() {
    std::lock_guard lk(mu_);
    thread_local std::mt19937_64 rng{
        0x1234ULL ^
        static_cast<unsigned long long>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()))};
    std::uniform_int_distribution<size_t> di(0, items_.size() - 1);
    return items_[di(rng)];
}

size_t Corpus::size() const {
    std::lock_guard lk(mu_);
    return items_.size();
}
