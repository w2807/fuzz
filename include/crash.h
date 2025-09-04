#ifndef FUZZ_CRASH_H
#define FUZZ_CRASH_H

#include <string>
#include <vector>

struct CrashInfo {
    bool crashed = false;
    std::string signature;
    std::string reason;
};

CrashInfo analyze_and_sig(
    int exit_code, int term_sig, bool timed_out, const std::string& out,
    const std::string& err, const std::vector<int>& allowed_exits);

#endif //FUZZ_CRASH_H
