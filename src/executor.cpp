#include "executor.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/resource.h>
#include <sys/wait.h>

#include "logger.h"
#include "utils.h"

namespace {
    int set_nonblock(const int fd) {
        const int f = fcntl(fd, F_GETFL, 0);
        return fcntl(fd, F_SETFL, f | O_NONBLOCK);
    }

    void set_rlimits(const int mem_mb) {
        if (mem_mb > 0) {
            rlimit rl{};
            rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(mem_mb) * 1024ull *
                1024ull;
            setrlimit(RLIMIT_AS, &rl);
        }
        rlimit fz{};
        fz.rlim_cur = fz.rlim_max = 64ull * 1024ull * 1024ull;
        setrlimit(RLIMIT_FSIZE, &fz);
    }

    void ignore_sigpipe() {
        struct sigaction sa{};
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGPIPE, &sa, nullptr);
    }
} // namespace

ExecResult Executor::run(const std::vector<std::string>& argv_t,
                         const std::vector<uint8_t>& data) const {
    ExecResult R;

    ignore_sigpipe();

    bool need_file = false, use_stdin = false;
    for (const auto& t : argv_t) {
        if (t == "@@") {
            need_file = true;
        }
        if (t == "{stdin}") {
            use_stdin = true;
        }
    }

    if (!need_file && !use_stdin) {
        need_file = true;
    }

    std::vector<std::string> args;
    args.reserve(argv_t.size() + 1);

    std::string tmpPath;
    int tmpFd = -1;

    auto cleanup_tmp = [&]() {
        if (tmpFd >= 0) {
            close(tmpFd);
            tmpFd = -1;
        }
        if (!tmpPath.empty()) {
            unlink(tmpPath.c_str());
            tmpPath.clear();
        }
    };

    if (need_file) {
        tmpFd = mktemp_file(tmpPath, "fuzz");
        if (tmpFd < 0) {
            R.exit_code = -1;
            R.err = "mktemp_file failed";
            cleanup_tmp();
            return R;
        }
        ssize_t off = 0;
        while (off < static_cast<ssize_t>(data.size())) {
            ssize_t w = write(tmpFd, data.data() + off, data.size() - off);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) {
                    continue;
                }
                R.exit_code = -1;
                R.err = "write(tmpfile) failed";
                cleanup_tmp();
                return R;
            }
            off += w;
        }
        fsync(tmpFd);
        lseek(tmpFd, 0, SEEK_SET);
        close(tmpFd);
        tmpFd = -1;
    }

    for (const auto& t : argv_t) {
        if (t == "@@") {
            args.push_back(tmpPath);
        } else if (t == "{stdin}") {} else {
            args.push_back(t);
        }
    }

    if (args.empty()) {
        R.err = "empty argv";
        R.exit_code = -1;
        cleanup_tmp();
        return R;
    }

    int in_pipe[2]{-1, -1}, out_pipe[2]{-1, -1}, err_pipe[2]{-1, -1};

    if (pipe(in_pipe) < 0) {
        R.exit_code = -1;
        R.err = "pipe() failed: in";
        cleanup_tmp();
        return R;
    }
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        R.exit_code = -1;
        R.err = "pipe() failed: out";
        cleanup_tmp();
        return R;
    }
    if (pipe(err_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        R.exit_code = -1;
        R.err = "pipe() failed: err";
        cleanup_tmp();
        return R;
    }

    pid_t pid = fork();
    if (pid < 0) {
        R.exit_code = -1;
        R.err = "fork() failed";
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        cleanup_tmp();
        return R;
    }

    if (pid == 0) {
        setsid();

        if (use_stdin) {
            dup2(in_pipe[0], STDIN_FILENO);
        } else {
            if (int devnull = open("/dev/null", O_RDONLY); devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);

        set_rlimits(cfg.mem_mb);

        std::vector<char*> av;
        av.reserve(args.size() + 1);
        for (auto& s : args) {
            av.push_back(const_cast<char*>(s.c_str()));
        }
        av.push_back(nullptr);

        execvp(av[0], av.data());
        std::perror("execvp");
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    set_nonblock(out_pipe[0]);
    set_nonblock(err_pipe[0]);

    if (!use_stdin) {
        close(in_pipe[1]);
        in_pipe[1] = -1;
    }

    size_t in_off = 0;
    uint64_t start = now_mono_ms();
    std::string outS, errS;

    auto drain = [](const int fd, std::string& dst) {
        char buf[8192];
        for (;;) {
            const ssize_t r = read(fd, buf, sizeof(buf));
            if (r > 0) {
                dst.append(buf, buf + r);
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EINTR)) {
                break;
            }
            break;
        }
    };

    for (;;) {
        struct pollfd pfds[3];
        int nfds = 0;
        if (in_pipe[1] != -1) {
            pfds[nfds++] = pollfd{in_pipe[1], POLLOUT, 0};
        }
        pfds[nfds++] = pollfd{out_pipe[0], POLLIN, 0};
        pfds[nfds++] = pollfd{err_pipe[0], POLLIN, 0};

        int elapsed = static_cast<int>(now_mono_ms() - start);
        int rem = std::max(1, cfg.timeout_ms - elapsed);

        if (int pr = ::poll(pfds, nfds, rem); pr < 0 && errno == EINTR) {
            continue;
        }

        drain(out_pipe[0], outS);
        drain(err_pipe[0], errS);

        if (use_stdin && in_pipe[1] != -1 && in_off < data.size()) {
            ssize_t w = ::write(in_pipe[1], data.data() + in_off,
                                data.size() - in_off);
            if (w > 0) {
                in_off += static_cast<size_t>(w);
            }
            if (w < 0 && errno != EAGAIN && errno != EINTR) {
                close(in_pipe[1]);
                in_pipe[1] = -1;
            }
            if (in_off == data.size()) {
                close(in_pipe[1]);
                in_pipe[1] = -1;
            }
        }

        int st = 0;
        if (pid_t wp = waitpid(pid, &st, WNOHANG); wp == pid) {
            if (WIFEXITED(st)) R.exit_code = WEXITSTATUS(st);
            if (WIFSIGNALED(st)) R.term_sig = WTERMSIG(st);
            drain(out_pipe[0], outS);
            drain(err_pipe[0], errS);
            break;
        }

        if (static_cast<int>(now_mono_ms() - start) >= cfg.timeout_ms) {
            R.timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            break;
        }
    }

    R.out = std::move(outS);
    R.err = std::move(errS);

    if (in_pipe[0] != -1) {
        close(in_pipe[0]);
    }
    if (in_pipe[1] != -1) {
        close(in_pipe[1]);
    }
    if (out_pipe[0] != -1) {
        close(out_pipe[0]);
    }
    if (out_pipe[1] != -1) {
        close(out_pipe[1]);
    }
    if (err_pipe[0] != -1) {
        close(err_pipe[0]);
    }
    if (err_pipe[1] != -1) {
        close(err_pipe[1]);
    }

    cleanup_tmp();
    return R;
}
