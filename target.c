#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static int read_all_stdin(uint8_t* buf, size_t cap) {
    size_t off = 0;
    while (off < cap) {
        ssize_t r = read(STDIN_FILENO, buf + off, cap - off);
        if (r <= 0) break;
        off += (size_t)r;
    }
    return (int)off;
}

static uint32_t rd32le(const uint8_t* p){
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int has_sub(const uint8_t* b, int n, const char* s) {
    const size_t m = strlen(s);
    if (m == 0 || n < (int)m) return 0;
    for (int i = 0; i + (int)m <= n; ++i) {
        if (memcmp(b + i, s, m) == 0) return 1;
    }
    return 0;
}

int main(void) {
    uint8_t buf[4096];
    int n = read_all_stdin(buf, sizeof(buf));
    if (n <= 0) {
        puts("empty");
        return 0;
    }

    int score = 0;

    if (n >= 2 && memcmp(buf, "AC", 2) == 0) {
        score++;
    }

    if (has_sub(buf, n, "FUC")) {
        score++;
    }

    for (int i = 0; i + 3 < n; ++i) {
        if (rd32le(&buf[i]) == 0xDEADBEEF) {
            score++;
            break;
        }
    }

    if (n >= 1 && buf[n - 1] == (uint8_t)((unsigned)n ^ 0x4A)) {
        score++;
    }

    if (score >= 2) {
        raise(SIGSEGV);
    } else {
        puts("ok");
    }
}