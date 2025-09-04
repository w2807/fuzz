#ifndef FUZZ_COVERAGE_H
#define FUZZ_COVERAGE_H

#include <cstdint>
#include <string>
#include <vector>

constexpr size_t kCoverageSize = 1 << 17;
constexpr char kCoverageVar[] = "__FUZZ_SHARE";

class Coverage {
public:
    Coverage();
    ~Coverage();

    bool setup();
    void reset() const;
    [[nodiscard]] bool has_new_edge() const;
    void merge();

    size_t collect_new_edges(std::vector<uint32_t>* out_edges = nullptr) const;

    [[nodiscard]] const std::string& shm_name() const {
        return shm_name_;
    }

private:
    int shm_id_ = -1;
    uint8_t* shm_map_ = nullptr;
    std::vector<uint8_t> total_coverage_;
    std::string shm_name_;
};

#endif //FUZZ_COVERAGE_H
