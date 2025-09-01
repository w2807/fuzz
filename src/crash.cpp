#include "crash.h"

#include <algorithm>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

static std::string trim(const std::string& s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) {
        i++;
    }
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) {
        j--;
    }
    return s.substr(i, j - i);
}

static std::string normalize_crash(const std::string& s) {
    std::string t = s;
    t = std::regex_replace(t, std::regex("==\\d+=="), "==PID==");
    t = std::regex_replace(t, std::regex("0x[0-9a-fA-F]+"), "0xX");
    t = std::regex_replace(t, std::regex("\\b[0-9a-fA-F]{8,}\\b"), "HEX");
    t = std::regex_replace(t, std::regex("\\b(pc|sp|bp|ip)\\s+0x[0-9a-fA-F]+"),
                           "$1 0xX");
    t = std::regex_replace(t, std::regex(":(\\d+)(?=\\b)"), ":*");
    return trim(t);
}

static std::string basename(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

static std::string normalize_line(const std::string& line) {
    std::string norm = normalize_crash(line);
    if (const size_t sp = norm.find_last_of(' '); sp != std::string::npos) {
        std::string tail = norm.substr(sp + 1);
        if (const size_t colon = tail.find(':'); colon != std::string::npos) {
            tail = basename(tail.substr(0, colon)) + ":*";
            norm.resize(sp + 1);
            norm.append(tail);
        }
    }
    return trim(norm);
}

static std::string top_frames(const std::string& combined) {
    constexpr int max_frames = 3;

    static const std::regex frame_re(R"(^\s*#\d+\s+.*)");
    static const char* noise[] = {
        "libasan", "__asan", "asan_", "__interceptor", "libc.so", "libstdc++",
        "libgcc", "ld-linux", "linux-vdso", "libpthread", "start_thread"
    };

    std::istringstream iss(combined);
    std::string line;
    std::vector<std::string> frames;
    frames.reserve(max_frames);

    while (std::getline(iss, line)) {
        if (!std::regex_search(line, frame_re)) {
            continue;
        }
        bool is_noise = false;
        for (const auto n : noise) {
            if (line.find(n) != std::string::npos) {
                is_noise = true;
                break;
            }
        }
        if (is_noise) {
            continue;
        }
        if (std::string norm = normalize_line(line); !norm.empty()) {
            frames.push_back(norm);
            if (frames.size() >= max_frames) {
                break;
            }
        }
    }

    std::ostringstream oss;
    for (size_t i = 0; i < frames.size(); i++) {
        if (i) {
            oss << " ; ";
        }
        oss << frames[i];
    }
    return oss.str();
}

static std::string asan_kind(const std::string& asan_first_line) {
    const std::string tag = "AddressSanitizer:";
    const size_t p = asan_first_line.find(tag);
    if (p == std::string::npos) {
        return normalize_crash(asan_first_line);
    }
    const std::string kind = asan_first_line.substr(p + tag.size());
    return normalize_crash(trim(kind));
}

static std::string first_line(const std::string& hay, const std::string& needle) {
    const size_t pos = hay.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    const size_t eol = hay.find('\n', pos);
    return trim(hay.substr(pos, eol == std::string::npos
                                    ? hay.size() - pos
                                    : eol - pos));
}

CrashInfo analyze_and_sig(int exit_code, int term_sig, bool timed_out,
                          const std::string& out, const std::string& err,
                          const std::vector<int>& allowed_exits) {
    CrashInfo ci;
    const std::string comb = out + "\n" + err;

    if (timed_out) {
        ci.crashed = true;
        ci.reason = "timeout";
        ci.signature = "timeout";
        return ci;
    }

    const std::string asan_err = first_line(comb, "ERROR: AddressSanitizer:");
    const std::string asan_deadly = first_line(
        comb, "AddressSanitizer:DEADLYSIGNAL");
    const bool has_asan = !asan_err.empty() || !asan_deadly.empty();
    const bool is_allowed_exit = std::ranges::find(allowed_exits, exit_code) !=
        allowed_exits.end();
    const bool exec_failed = (exit_code == 127) && (err.find("execvp:") !=
        std::string::npos);

    if (const bool runner_error = (exit_code < 0); exec_failed ||
        runner_error) {
        ci.crashed = false;
        ci.reason = exec_failed ? "execvp" : "runner";
        ci.signature.clear();
        return ci;
    }

    if (term_sig) {
        ci.crashed = true;
        ci.reason = "signal:" + std::to_string(term_sig);
    } else if (has_asan) {
        ci.crashed = true;
        ci.reason = "asan";
    } else if (exit_code != 0 && !is_allowed_exit) {
        ci.crashed = true;
        ci.reason = "exit:" + std::to_string(exit_code);
    } else {
        ci.crashed = false;
    }

    std::string sig_src;
    if (has_asan) {
        const std::string asan_first = !asan_err.empty()
                                           ? asan_err
                                           : asan_deadly;
        const std::string kind = asan_kind(asan_first);
        const std::string tops = top_frames(comb);
        sig_src = "asan|" + kind + "|" + tops;
    } else if (term_sig) {
        const std::string tops = top_frames(comb);
        sig_src = "sig|" + std::to_string(term_sig) + "|" + tops;
    } else {
        sig_src = "rc|" + std::to_string(exit_code);
    }

    std::hash<std::string> h;
    std::ostringstream oss;
    oss << std::hex << h(sig_src);
    ci.signature = oss.str();
    return ci;
}
