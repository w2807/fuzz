#include "corpus.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "logger.h"

Corpus::Corpus(const size_t max_size_bytes, const size_t max_items) :
    max_size_bytes_(max_size_bytes), cap_(max_items) {}

bool Corpus::load_dir(const std::string& dir) {
    size_t n = 0, skipped = 0;
    for (std::error_code ec; auto& p :
         std::filesystem::directory_iterator(dir, ec)) {
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
        add(b, 1);
        n++;
    }
    logx::info(
        "loaded seeds: " + std::to_string(n) + " skipped: " + std::to_string(
            skipped));
    if (size() == 0) {
        std::vector<uint8_t> def = {'s', 'e', 'e', 'd'};
        add(def, 1);
    }
    return size() > 0;
}

void Corpus::add(const std::vector<uint8_t>& item, uint32_t score) {
    std::lock_guard lk(mu_);
    if (items_.size() >= cap_) {
        return;
    }

    Entry e;
    e.data = item;
    if (e.data.size() > max_size_bytes_) {
        e.data.resize(max_size_bytes_);
    }
    e.score = score == 0 ? 1u : score;
    e.picks = 0;
    items_.push_back(std::move(e));
}

std::vector<uint8_t> Corpus::pick() {
    std::lock_guard lk(mu_);
    if (items_.empty()) {
        return {};
    }

    thread_local std::mt19937_64 rng{
        0x1234ULL ^
        static_cast<unsigned long long>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()))
    };

    long double total_w = 0.0L;
    std::vector<long double> weights;
    weights.reserve(items_.size());
    for (const auto& e : items_) {
        const long double decay = 1.0L + static_cast<long double>(e.picks) /
            8.0L;
        long double w = static_cast<long double>(e.score) / decay;
        if (w < 1.0L) {
            w = 1.0L;
        }
        weights.push_back(w);
        total_w += w;
    }

    std::uniform_real_distribution dist(0.0L, total_w);
    long double cut = dist(rng);

    for (size_t i = 0; i < items_.size(); ++i) {
        if (cut <= weights[i]) {
            items_[i].picks++;
            return items_[i].data;
        }
        cut -= weights[i];
    }
    items_.back().picks++;
    return items_.back().data;
}

size_t Corpus::size() const {
    std::lock_guard lk(mu_);
    return items_.size();
}
