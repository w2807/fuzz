#ifndef FUZZ_LOGGER_H
#define FUZZ_LOGGER_H

#include <cstdio>
#include <mutex>
#include <string>

namespace logx {
inline std::mutex& mu() {
    static std::mutex m;
    return m;
}

inline void info(const std::string& s) {
    std::lock_guard lk(mu());
    std::fprintf(stderr, "[i] %s\n", s.c_str());
}

inline void warn(const std::string& s) {
    std::lock_guard lk(mu());
    std::fprintf(stderr, "[!] %s\n", s.c_str());
}

inline void good(const std::string& s) {
    std::lock_guard lk(mu());
    std::fprintf(stderr, "[+] %s\n", s.c_str());
}
} // namespace logx

#endif //FUZZ_LOGGER_H
