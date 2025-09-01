#include "crash.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>

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

static std::string first_line_containing(const std::string& hay,
                                         const char* needle) {
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

    const std::string asan_err = first_line_containing(
        comb, "ERROR: AddressSanitizer:");
    const std::string asan_deadly = first_line_containing(
        comb, "AddressSanitizer:DEADLYSIGNAL");
    const bool has_asan = !asan_err.empty() || !asan_deadly.empty();

    const bool is_allowed_exit = std::ranges::find(allowed_exits,
                                                   exit_code) != allowed_exits.
        end();

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
        sig_src = "asan:" + (!asan_err.empty() ? asan_err : asan_deadly);
    } else if (term_sig) {
        sig_src = "sig:" + std::to_string(term_sig);
    } else {
        sig_src = "rc:" + std::to_string(exit_code);
    }

    std::hash<std::string> h;
    std::ostringstream oss;
    oss << std::hex << h(sig_src);
    ci.signature = oss.str();
    return ci;
}
