#include "coverage.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <sys/mman.h>

#include "logger.h"

Coverage::Coverage() :
    total_coverage_(kCoverageSize, 0) {}

Coverage::~Coverage() {
    if (shm_map_) {
        munmap(shm_map_, kCoverageSize);
    }
    if (shm_id_ >= 0) {
        close(shm_id_);
        if (!shm_name_.empty()) {
            shm_unlink(shm_name_.c_str());
        }
    }
}

bool Coverage::setup() {
    static std::atomic<uint32_t> g_cov_cnt{0};
    char name_buf[48];
    const uint32_t idx = ++g_cov_cnt;
    snprintf(name_buf, sizeof(name_buf), "/fuzz_%d_%u", getpid(), idx);
    shm_name_.assign(name_buf);

    shm_id_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0600);
    if (shm_id_ < 0) {
        logx::warn("shm_open failed");
        return false;
    }

    if (ftruncate(shm_id_, kCoverageSize) < 0) {
        logx::warn("ftruncate failed");
        close(shm_id_);
        shm_unlink(shm_name_.c_str());
        return false;
    }

    shm_map_ = static_cast<uint8_t*>(
        mmap(nullptr, kCoverageSize, PROT_READ | PROT_WRITE, MAP_SHARED,
             shm_id_, 0));

    if (shm_map_ == MAP_FAILED) {
        logx::warn("mmap failed");
        close(shm_id_);
        shm_unlink(shm_name_.c_str());
        return false;
    }

    // 不在父进程全局 setenv；由 Executor 在子进程里设置
    return true;
}

void Coverage::reset() const {
    if (shm_map_) {
        std::memset(shm_map_, 0, kCoverageSize);
    }
}

bool Coverage::has_new_edge() const {
    if (!shm_map_) {
        return false;
    }
    for (size_t i = 0; i < kCoverageSize; ++i) {
        if (shm_map_[i] && !total_coverage_[i]) {
            return true;
        }
    }
    return false;
}

void Coverage::merge() {
    if (!shm_map_) {
        return;
    }
    for (size_t i = 0; i < kCoverageSize; ++i) {
        if (shm_map_[i]) {
            total_coverage_[i] = 1;
        }
    }
}

size_t Coverage::collect_new_edges(std::vector<uint32_t>* out_edges) const {
    if (!shm_map_)
        return 0;
    size_t cnt = 0;
    for (size_t i = 0; i < kCoverageSize; ++i) {
        if (shm_map_[i] && !total_coverage_[i]) {
            ++cnt;
            if (out_edges) {
                out_edges->push_back(static_cast<uint32_t>(i));
            }
        }
    }
    return cnt;
}
