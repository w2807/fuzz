#ifndef FUZZ_EXECUTOR_H
#define FUZZ_EXECUTOR_H

#include <cstdint>
#include <string>
#include <vector>

struct ExecResult {
    int exit_code = 0;
    int term_sig = 0;
    bool timed_out = false;
    std::string out;
    std::string err;
};

struct ExecConfig {
    int timeout_ms = 1000;
    int mem_mb = 0;
    const char* cov_shm_name = nullptr;
};

class Executor {
public:
    explicit Executor(const ExecConfig cfg) : cfg_(cfg) {}

    [[nodiscard]] ExecResult run(
        const std::vector<std::string>& argv_t,
        const std::vector<uint8_t>& data) const;

private:
    ExecConfig cfg_;
};

#endif //FUZZ_EXECUTOR_H
