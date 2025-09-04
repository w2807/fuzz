#include "utils.h"

#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <random>
#include <sstream>
#include <sys/stat.h>

std::vector<std::string> split_cmdline(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_s = false, in_d = false, esc = false;
    for (const char c : s) {
        if (esc) {
            cur.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (!in_s && c == '"') {
            in_d = !in_d;
            continue;
        }
        if (!in_d && c == '\'') {
            in_s = !in_s;
            continue;
        }
        if (!in_s && !in_d && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == '/') {
        return a + b;
    }
    return a + "/" + b;
}

std::string now_iso8601() {
    using namespace std::chrono;
    const auto t = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    const auto ms = duration_cast<milliseconds>(t.time_since_epoch()).count() %
        1000;
    char buf2[80];
    std::snprintf(buf2, sizeof(buf2), "%s.%03lld", buf,
                  static_cast<long long>(ms));
    return buf2;
}

uint64_t now_mono_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).
        count();
}

int mktemp_file(std::string& path, const std::string& prefix) {
    path = "/tmp/" + prefix + ".XXXXXX";
    std::vector buf(path.begin(), path.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());
    if (fd >= 0) {
        path.assign(buf.data());
        fchmod(fd, 0600);
    }
    return fd;
}

uint64_t seed_from_os() {
    std::random_device rd;
    const uint64_t a = static_cast<uint64_t>(rd()) << 32 ^ rd();
    const uint64_t b = static_cast<uint64_t>(rd()) << 32 ^ rd();
    return a ^ b ^ now_mono_ms();
}
