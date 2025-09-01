#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <unordered_set>

#include "corpus.h"
#include "crash.h"
#include "executor.h"
#include "logger.h"
#include "mutations.h"
#include "options.h"
#include "utils.h"

static void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s --target \"./prog @@/{stdin}\" --seeds dir --out dir [opts]\n"
                 "  --iterations N        total testcases (default 10000)\n"
                 "  --threads N           parallel workers (default 1)\n"
                 "  --timeout-ms N        per-run timeout (default 1000)\n"
                 "  --mem-mb N            RLIMIT_AS in MB (default 0 unlimited)\n"
                 "  --max-size N          max testcase bytes (default 4096)\n"
                 "  --dict path           dictionary file\n"
                 "  --seed N              rng seed (default random)\n"
                 "  --allowed-exits CSV   e.g. 1,2,3 treated as non-crash\n",
                 prog);
}

bool parse_options(const int argc, char** argv, Options& o, std::string& err) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const int k) {
            if (i + k >= argc) {
                err = "missing value for " + a;
                return false;
            }
            return true;
        };

        if (a == "--target") {
            if (!need(1)) {
                return false;
            }
            o.target = argv[++i];
        } else if (a == "--seeds") {
            if (!need(1)) {
                return false;
            }
            o.seeds_dir = argv[++i];
        } else if (a == "--out") {
            if (!need(1)) {
                return false;
            }
            o.out_dir = argv[++i];
        } else if (a == "--iterations") {
            if (!need(1)) {
                return false;
            }
            o.iterations = std::stoi(argv[++i]);
        } else if (a == "--threads") {
            if (!need(1)) {
                return false;
            }
            o.threads = std::stoi(argv[++i]);
        } else if (a == "--timeout-ms") {
            if (!need(1)) {
                return false;
            }
            o.timeout_ms = std::stoi(argv[++i]);
        } else if (a == "--mem-mb") {
            if (!need(1)) {
                return false;
            }
            o.mem_mb = std::stoi(argv[++i]);
        } else if (a == "--max-size") {
            if (!need(1)) {
                return false;
            }
            o.max_size = (size_t)std::stoul(argv[++i]);
        } else if (a == "--dict") {
            if (!need(1)) {
                return false;
            }
            o.dict_path = argv[++i];
        } else if (a == "--seed") {
            if (!need(1)) {
                return false;
            }
            o.seed = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (a == "--allowed-exits") {
            if (!need(1)) {
                return false;
            }
            std::string v = argv[++i];
            size_t pos = 0;
            while (pos < v.size()) {
                const size_t c = v.find(',', pos);
                std::string tok = v.substr(
                    pos, c == std::string::npos ? v.size() - pos : c - pos);
                if (!tok.empty()) {
                    o.allowed_exits.insert(std::stoi(tok));
                }
                if (c == std::string::npos) {
                    break;
                }
                pos = c + 1;
            }
        } else {
            err = "unknown arg: " + a;
            return false;
        }
    }
    if (o.target.empty() || o.seeds_dir.empty() || o.out_dir.empty()) {
        err = "missing required args";
        return false;
    }
    if (o.threads < 1) {
        o.threads = 1;
    }
    return true;
}

struct Shared {
    Corpus corpus;
    std::unordered_set<std::string> seen;
    std::mutex seen_mu;
    std::atomic<uint64_t> iter_done{0};
    std::atomic<uint64_t> crashes{0};
    std::atomic<uint64_t> saved{0};

    explicit Shared(const size_t max_size) :
        corpus(max_size) {}
};

static void save_crash(const std::string& out_dir, uint64_t id,
                       const std::vector<uint8_t>& buf,
                       const ExecResult& R, const CrashInfo& C) {
    std::filesystem::create_directories(out_dir);
    std::string base = "crash-" + std::to_string(id);
    std::string bin = join_path(out_dir, base + ".bin");
    std::string meta = join_path(out_dir, base + ".meta.txt");
    std::ofstream of(bin, std::ios::binary);
    const size_t sz = buf.size();

    if (sz > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        logx::warn("buffer too large");
        return;
    }
    of.write(reinterpret_cast<const char*>(buf.data()),
             static_cast<std::streamsize>(sz));
    std::ofstream mf(meta);
    mf << "time: " << now_iso8601() << "\n";
    mf << "reason: " << C.reason << "\n";
    mf << "sig: " << C.signature << "\n";
    mf << "exit: " << R.exit_code << " term_sig: " << R.term_sig << " timeout: "
        << (R.timed_out ? "yes" : "no") << "\n";
    mf << "stdout:\n" << R.out << "\n--- stderr ---\n" << R.err << "\n";
}

static bool preflight_target(const std::vector<std::string>& argv_t,
                             std::string& err) {
    if (argv_t.empty()) {
        err = "empty target";
        return false;
    }
    const std::string& exe = argv_t[0];

    auto is_exec = [](const std::string& p) {
        return access(p.c_str(), X_OK) == 0;
    };

    if (exe.find('/') != std::string::npos) {
        if (!is_exec(exe)) {
            err = "target not executable: " + exe + " (" + std::string(
                std::strerror(errno)) + ")";
            return false;
        }
    } else {
        const char* path = std::getenv("PATH");
        if (!path) {
            err = "PATH is empty; cannot locate target: " + exe;
            return false;
        }
        std::string p(path);
        size_t pos = 0;
        bool found = false;
        while (pos <= p.size()) {
            const size_t sep = p.find(':', pos);
            std::string dir = p.substr(
                pos, sep == std::string::npos ? p.size() - pos : sep - pos);
            if (!dir.empty()) {
                if (std::string full = join_path(dir, exe); is_exec(full)) {
                    found = true;
                    break;
                }
            }
            if (sep == std::string::npos) {
                break;
            }
            pos = sep + 1;
        }
        if (!found) {
            err = "cannot find target in PATH: " + exe;
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    Options opt;
    if (std::string err; !parse_options(argc, argv, opt, err)) {
        usage(argv[0]);
        if (!err.empty()) {
            logx::warn(err);
        }
        return 1;
    }

    std::filesystem::create_directories(opt.out_dir);

    Shared shared(opt.max_size);
    if (!shared.corpus.load_dir(opt.seeds_dir)) {
        logx::warn("failed to load seeds");
        return 1;
    }

    Dict dict;
    if (!opt.dict_path.empty()) {
        if (load_dict(opt.dict_path, dict)) {
            logx::info(
                "dict loaded: " + std::to_string(dict.tokens.size()));
        } else {
            logx::warn("dict empty or load failed");
        }
    }

    auto argv_template = split_cmdline(opt.target);
    if (argv_template.empty()) {
        logx::warn("empty target");
        return 1;
    }
    if (std::string terr; !preflight_target(argv_template, terr)) {
        logx::warn(terr);
        return 1;
    }

    uint64_t global_seed = opt.seed ? opt.seed : seed_from_os();
    logx::info("seed: " + std::to_string(global_seed));

    std::atomic<uint64_t> crash_id{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < opt.threads; t++) {
        workers.emplace_back([&, t] {
            const uint64_t seed = global_seed ^ (0x9e3779b97f4a7c15ULL +
                static_cast<uint64_t>(t) *
                0x5851f42d4c957f2dULL);
            Mutator mut(seed, opt.max_size,
                        dict.tokens.empty() ? nullptr : &dict);
            const Executor exec(ExecConfig{opt.timeout_ms, opt.mem_mb});
            const std::vector<int> allowed(opt.allowed_exits.begin(),
                                           opt.allowed_exits.end());

            while (true) {
                const uint64_t done = shared.iter_done.fetch_add(1);
                if (static_cast<int64_t>(done) >= opt.iterations) {
                    break;
                }

                auto base = shared.corpus.pick();
                std::vector<uint8_t> test;
                if ((seed + done) % 5 == 0 && shared.corpus.size() >= 2) {
                    auto other = shared.corpus.pick();
                    test = mut.crossover(base, other);
                } else {
                    test = mut.mutate(base);
                }

                ExecResult R = exec.run(argv_template, test);

                CrashInfo C = analyze_and_sig(R.exit_code, R.term_sig,
                                              R.timed_out, R.out, R.err,
                                              allowed);
                if (C.crashed) {
                    std::lock_guard<std::mutex> lk(shared.seen_mu);
                    if (shared.seen.insert(C.signature).second) {
                        const uint64_t id = crash_id.fetch_add(1);
                        save_crash(opt.out_dir, id, test, R, C);
                        shared.saved.fetch_add(1);
                        logx::good(
                            "new crash sig=" + C.signature + " id=" +
                            std::to_string(id) + " reason=" + C.reason);
                    }
                    shared.crashes.fetch_add(1);
                } else {
                    if (((seed + done) & 0xFF) < 3) {
                        shared.corpus.add(
                            test);
                    }
                }

                if ((done + 1) % 1000 == 0) {
                    logx::info(
                        "iter " + std::to_string(done + 1) + "/" +
                        std::to_string(opt.iterations) +
                        " crashes=" + std::to_string(shared.crashes.load()) +
                        " saved=" + std::to_string(shared.saved.load()) +
                        " seeds=" + std::to_string(shared.corpus.size()));
                }
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    logx::info("done. total=" + std::to_string(shared.iter_done.load()) +
        " crashes=" + std::to_string(shared.crashes.load()) +
        " saved=" + std::to_string(shared.saved.load()));
    return 0;
}
